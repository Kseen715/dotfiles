/* yaml_test.c -- the vendored yaml.h builds in this tree and resolves anchors.
 *
 * LibYAML is upstream code with its own suite; this is not a second copy of
 * it. What it checks is the vendoring: that the amalgamated single header
 * compiles as one translation unit under the same C89 flags as the rest of
 * the tree, and that the one feature the vendoring was chosen for -- anchors
 * and aliases, which a subset parser would not have -- survives the load.
 */
#define YAML_IMPLEMENTATION
#include "../../thirdparty/yaml.h"

#include "../c_test.h"

static const char *DOC =
    "base: &b\n"
    "  host: example.com\n"
    "  port: 8080\n"
    "dev:\n"
    "  <<: *b\n"
    "  port: 9090\n"
    "list: [*b]\n";

/* value_of -- the value node id for a top-level key, or 0 if absent. */
static int value_of(yaml_document_t *doc, yaml_node_t *map, const char *key) {
    yaml_node_pair_t *pair;
    for (pair = map->data.mapping.pairs.start;
         pair < map->data.mapping.pairs.top; pair++) {
        yaml_node_t *k = yaml_document_get_node(doc, pair->key);
        if (k && k->type == YAML_SCALAR_NODE &&
            strcmp((const char *)k->data.scalar.value, key) == 0) {
            return pair->value;
        }
    }
    return 0;
}

int main(void) {
    yaml_parser_t parser;
    yaml_document_t doc;
    yaml_node_t *root, *host;
    int anchored, aliased, first;

    OSR_T_INIT();

    osr_t_eq_str("version is the vendored one", yaml_get_version_string(), "0.2.5");

    if (!yaml_parser_initialize(&parser)) {
        osr_t_fail_msg("parser initialises", "yaml_parser_initialize failed");
        return osr_t_finish();
    }
    yaml_parser_set_input_string(&parser, (const unsigned char *)DOC, strlen(DOC));

    if (!yaml_parser_load(&parser, &doc)) {
        osr_t_fail_msg("document loads", parser.problem ? parser.problem : "(no problem set)");
        yaml_parser_delete(&parser);
        return osr_t_finish();
    }
    osr_t_ok("document loads");

    root = yaml_document_get_root_node(&doc);
    osr_t_eq_int("root is a mapping", root ? (int)root->type : -1, YAML_MAPPING_NODE);

    /* An alias is the anchored node, not a copy: `*b` under "list" must come
     * back as the same node id "base" was given. */
    anchored = value_of(&doc, root, "base");
    aliased = value_of(&doc, root, "list");
    first = 0;
    if (aliased) {
        yaml_node_t *seq = yaml_document_get_node(&doc, aliased);
        if (seq && seq->type == YAML_SEQUENCE_NODE) first = *seq->data.sequence.items.start;
    }
    osr_t_eq_int("alias resolves to the anchored node", first, anchored);

    /* And the anchored node still carries its own content. */
    host = yaml_document_get_node(&doc, anchored);
    osr_t_eq_str("anchored mapping keeps its value",
                 host ? (const char *)yaml_document_get_node(&doc, value_of(&doc, host, "host"))
                            ->data.scalar.value
                      : NULL,
                 "example.com");

    yaml_document_delete(&doc);
    yaml_parser_delete(&parser);
    return osr_t_finish();
}
