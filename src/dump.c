/* Parse one descriptor and print everything the parser derived from it.
 *
 *   ./dump                 list descriptor names, one per line
 *   ./dump <name>          parse it and dump the result
 *
 * Output is deliberately stable and line oriented so `make compare` can diff two
 * builds of it against each other. Any change to this format invalidates nothing,
 * but both sides of a comparison must be built from the same dump.c.
 */
#include "main.h"
#include "descriptors.h"

static void dump_val(const char *label, report_val_t *v) {
    printf("    %-10s off=%-5u idx=%-4u size=%-3u usage=0x%04X page=0x%04X gusage=0x%04X "
           "rid=%-3u umin=%-6d umax=%-6d %s %s\n",
           label, v->offset, v->offset_idx, v->size, v->usage, v->usage_page, v->global_usage,
           v->report_id, v->usage_min, v->usage_max,
           v->item_type == CONSTANT ? "const" : "data",
           v->data_type == VARIABLE ? "var" : "arr");
}

static void dump_iface(hid_interface_t *iface) {
    printf("  uses_report_id=%d protocol=%u\n", iface->uses_report_id, iface->protocol);

    printf("  mouse: found=%d rid=%u uses_rid=%d\n", iface->mouse.is_found, iface->mouse.report_id,
           iface->mouse.uses_report_id);
    if (iface->mouse.is_found) {
        dump_val("buttons", &iface->mouse.buttons);
        dump_val("move_x", &iface->mouse.move_x);
        dump_val("move_y", &iface->mouse.move_y);
        dump_val("wheel", &iface->mouse.wheel);
        dump_val("pan", &iface->mouse.pan);
    }

    printf("  keyboards: %u\n", iface->num_keyboards);
    for (int k = 0; k < iface->num_keyboards && k < MAX_KEYBOARDS; k++) {
        keyboard_t *kb = &iface->keyboards[k];

        printf("    kbd[%d] rid=%u found=%d nkro=%d uses_rid=%d keys=", k, kb->report_id,
               kb->is_found, kb->is_nkro, kb->uses_report_id);
        for (int j = 0; j < MAX_KEYS; j++)
            printf("%d", kb->key_array[j]);
        printf("\n");

        dump_val("modifier", &kb->modifier);

        /* keyboard_t differs by branch: main carries a single nkro field, the
           multi-block branch carries an array. Detect on the macro it adds. */
#ifdef MAX_NKRO_BLOCKS
        printf("      nkro_count=%u\n", kb->nkro_count);
        for (int j = 0; j < kb->nkro_count && j < MAX_NKRO_BLOCKS; j++)
            printf("      nkro[%d] off=%u size=%u umin=%u umax=%u\n", j, kb->nkro[j].offset,
                   kb->nkro[j].size, kb->nkro[j].usage_min, kb->nkro[j].usage_max);
#else
        dump_val("nkro", &kb->nkro);
#endif

    }

    /* cc_array and sys_array live on a keyboard_t, but they are consumer and system
       state: handle_consumer_control_values() writes them through get_keyboard(),
       and process_consumer_report() reads them back the same way. Printing them
       under the keyboard loop meant they vanished whenever num_keyboards was 0 -
       which is exactly the case for cherry_kc6000_consumer, a consumer-only
       interface whose cc_array is the whole point. Print them where they are used,
       and read keyboards[PRIMARY_KEYBOARD] the way get_keyboard() does when an
       interface has no report IDs. */
    const keyboard_t *cckb = &iface->keyboards[PRIMARY_KEYBOARD];

    printf("  consumer: rid=%u var=%d arr=%d\n", iface->consumer.report_id,
           iface->consumer.is_variable, iface->consumer.is_array);
    dump_val("consumer", &iface->consumer.val);
    printf("      cc:");
    for (int j = 0; j < MAX_CC_BUTTONS; j++)
        printf(" %04X", cckb->cc_array[j]);
    printf("\n");

    printf("  system: rid=%u var=%d arr=%d\n", iface->system.report_id, iface->system.is_variable,
           iface->system.is_array);
    dump_val("system", &iface->system.val);
    printf("      sys:");
    for (int j = 0; j < MAX_SYS_BUTTONS; j++)
        printf(" %04X", cckb->sys_array[j]);
    printf("\n");

    printf("  handlers:");
    for (int i = 0; i < MAX_REPORTS; i++) {
        const char *n = ".";
        if (iface->report_handler[i] == process_mouse_report)      n = "M";
        else if (iface->report_handler[i] == process_keyboard_report) n = "K";
        else if (iface->report_handler[i] == process_consumer_report) n = "C";
        else if (iface->report_handler[i] == process_system_report)   n = "S";
        printf("%s", n);
    }
    printf("\n");
}

int main(int argc, char **argv) {
    static hid_interface_t iface;

    if (argc < 2) {
        for (unsigned i = 0; i < ARRAY_SIZE(descriptors); i++)
            printf("%s\n", descriptors[i].name);
        return 0;
    }

    const descriptor_t *d = find_descriptor(argv[1]);
    if (!d) {
        fprintf(stderr, "dump: no descriptor named '%s'\n", argv[1]);
        return 2;
    }

    memset(&iface, 0, sizeof(iface));
    iface.protocol = HID_PROTOCOL_REPORT;

    printf("%s (%d bytes)\n", d->name, d->len);
    parse_report_descriptor(&iface, d->bytes, d->len);
    dump_iface(&iface);

    return 0;
}
