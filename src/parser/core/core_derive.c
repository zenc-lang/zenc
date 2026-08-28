// SPDX-License-Identifier: MIT
#include "parser.h"
#include "constants.h"
#include "ast/ast.h"
#include "analysis/move_check.h"
#include "plugins/plugin_manager.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "analysis/const_fold.h"
#include "utils/utils.h"
#include "ast/primitives.h"

ASTNode *generate_derive_impls(ParserContext *ctx, ASTNode *strct, char **traits, int count)
{
    ASTNode *head = NULL, *tail = NULL;
    char *name = strct->strct.name;

    for (int i = 0; i < count; i++)
    {
        char *trait = traits[i];
        char *code = NULL;

        if (0 == strcmp(trait, "Clone"))
        {
            code = xmalloc(1024);
            sprintf(code, "impl %s { fn clone(self) -> %s { return self; } }", name,
                    name); /* safe */
        }
        else if (0 == strcmp(trait, "Eq"))
        {
            char body[4096];
            body[0] = 0;

            if (strct->kind == NODE_ENUM)
            {
                // Check if enum has payloads
                int has_payload = 0;
                ASTNode *ev = strct->enm.variants;
                while (ev)
                {
                    if (ev->variant.payload)
                    {
                        has_payload = 1;
                        break;
                    }
                    ev = ev->next;
                }

                if (has_payload)
                {
                    snprintf(body, sizeof(body),
                             "return self.tag == other.tag;"); /* TODO: check buffer size */
                }
                else
                {
                    // Simple enum: direct comparison via raw C (no .tag)
                    snprintf(body, sizeof(body),
                             "raw { return *self == *other; }"); /* TODO: check buffer size */
                }
            }
            else
            {
                ASTNode *f = strct->strct.fields;
                int first = 1;
                strncat(body, "return ", sizeof(body) - strlen(body) - 1);
                while (f)
                {
                    if (f->kind == NODE_FIELD)
                    {
                        char *fn = f->field.name;
                        char *ft = f->field.type;
                        if (!first)
                        {
                            strncat(body, " && ", sizeof(body) - strlen(body) - 1);
                        }
                        char cmp[MAX_VAR_NAME_LEN];

                        // Detect pointer using type_info OR string check (fallback)
                        int is_ptr = 0;
                        if (f->type_info && f->type_info->kind == TYPE_POINTER)
                        {
                            is_ptr = 1;
                        }
                        // Fallback: check if type string ends with '*'
                        if (!is_ptr && ft && strchr(ft, '*'))
                        {
                            is_ptr = 1;
                        }

                        // Only look up struct def for non-pointer types
                        ASTNode *fdef = is_ptr ? NULL : find_struct_def(ctx, ft);

                        if (!is_ptr && fdef && fdef->kind == NODE_ENUM)
                        {
                            // Check if enum is simple (no payloads)
                            int ep = 0;
                            ASTNode *ev2 = fdef->enm.variants;
                            while (ev2)
                            {
                                if (ev2->variant.payload)
                                {
                                    ep = 1;
                                    break;
                                }
                                ev2 = ev2->next;
                            }
                            if (ep)
                            {
                                snprintf(cmp, sizeof(cmp), "self.%s.tag == other.%s.tag", fn,
                                         fn); /* TODO: check buffer size */
                            }
                            else
                            {
                                // Simple enum: direct comparison
                                snprintf(cmp, sizeof(cmp), "self.%s == other.%s", fn,
                                         fn); /* TODO: check buffer size */
                            }
                        }
                        else if (!is_ptr && fdef && fdef->kind == NODE_STRUCT)
                        {
                            // Struct field: use __eq function
                            snprintf(cmp, sizeof(cmp), "%s__eq(&self.%s, &other.%s)", ft, fn,
                                     fn); /* TODO: check buffer size */
                        }
                        else
                        {
                            // Primitive, POINTER, or unknown: use ==
                            snprintf(cmp, sizeof(cmp), "self.%s == other.%s", fn,
                                     fn); /* TODO: check buffer size */
                        }
                        strncat(body, cmp, sizeof(body) - strlen(body) - 1);
                        first = 0;
                    }
                    f = f->next;
                }
                if (first)
                {
                    strncat(body, "true", sizeof(body) - strlen(body) - 1);
                }
                strncat(body, ";", sizeof(body) - strlen(body) - 1);
            }
            code = xmalloc(4096 + 1024);
            // Updated signature: other is a pointer T*
            sprintf(code, "impl %s { fn eq(self, other: %s*) -> bool { %s } }", name, name,
                    body); /* safe */
        }
        else if (0 == strcmp(trait, "Debug"))
        {
            // Simplistic Debug for now, I know.
            code = xmalloc(1024);
            sprintf(
                code,
                "impl %s { fn to_string(self) -> char* { return \"%s {{ ... }}\"; } }", /* safe */
                name, name);
        }
        else if (0 == strcmp(trait, "Copy"))
        {
            // Marker trait for Copy/Move semantics
            code = xmalloc(1024);
            sprintf(code, "impl Copy for %s {}", name); /* safe */
        }
        else if (0 == strcmp(trait, "FromJson"))
        {
            // Generate from_json(j: JsonValue*) -> Result<StructName>
            // Only works for structs (not enums)
            if (strct->kind != NODE_STRUCT)
            {
                zwarn_at(strct->token, "@derive(FromJson) only works on structs");
                continue;
            }

            char body[8192];
            body[0] = 0;

            // Track Vec<String> fields for forget calls
            char *vec_fields[32];
            int vec_field_count = 0;

            // Build field assignments
            ASTNode *f = strct->strct.fields;
            while (f)
            {
                if (f->kind == NODE_FIELD)
                {
                    char *fn = f->field.name;
                    char *ft = f->field.type;
                    char assign[2048];

                    if (!fn || !ft)
                    {
                        f = f->next;
                        continue;
                    }

                    // Map types to appropriate get_* calls
                    int is_int_type = strcmp(ft, "int") == 0 || strcmp(ft, "int32_t") == 0 ||
                                      strcmp(ft, "i32") == 0 || strcmp(ft, "i64") == 0 ||
                                      strcmp(ft, "int64_t") == 0 || strcmp(ft, "u32") == 0 ||
                                      strcmp(ft, "uint32_t") == 0 || strcmp(ft, "u64") == 0 ||
                                      strcmp(ft, "uint64_t") == 0 || strcmp(ft, "usize") == 0 ||
                                      strcmp(ft, "size_t") == 0;
                    if (is_int_type)
                    {
                        snprintf(assign, 2048, "let _f_%s = (*j).get_int(\"%s\").unwrap_or(0);\n",
                                 fn, fn);
                    }
                    else if (strcmp(ft, "double") == 0)
                    {
                        snprintf(assign, sizeof(assign),
                                 "let _f_%s = (*j).get_float(\"%s\").unwrap_or(0.0);\n",
                                 fn, /* TODO: check buffer size */
                                 fn);
                    }
                    else if (strcmp(ft, "bool") == 0)
                    {
                        snprintf(assign, sizeof(assign),
                                 "let _f_%s = (*j).get_bool(\"%s\").unwrap_or(false);\n",
                                 fn, /* TODO: check buffer size */
                                 fn);
                    }
                    else if (strcmp(ft, "char*") == 0)
                    {
                        snprintf(
                            assign, sizeof(assign),
                            "let _f_%s = (*j).get_string(\"%s\").unwrap_or(\"\");\n", /* TODO: check
                                                                                         buffer size
                                                                                       */
                            fn, fn);
                    }
                    else if (strcmp(ft, "String") == 0)
                    {
                        sprintf(/* TODO: check buffer size */
                                assign,
                                "let _f_%s = "
                                "String::new((*j).get_string(\"%s\").unwrap_or(\"\"));\n",
                                fn, fn);
                    }
                    else if (ft && strstr(ft, "Vec") && strstr(ft, "String"))
                    {
                        // Track this field for forget() call later
                        if (vec_field_count < 32)
                        {
                            vec_fields[vec_field_count++] = fn;
                        }
                        sprintf(/* TODO: check buffer size */
                                assign,
                                "let _f_%s = Vec<String>::new();\n"
                                "let _arr_%s = (*j).get_array(\"%s\");\n"
                                "if _arr_%s.is_some() {\n"
                                "  let _a_%s = _arr_%s.unwrap();\n"
                                "  for let _i_%s: usize = 0; _i_%s < _a_%s.len(); _i_%s = _i_%s + "
                                "1 {\n"
                                "    let _item_%s = _a_%s.at(_i_%s);\n"
                                "    if _item_%s.is_some() {\n"
                                "      let _str_%s = (*_item_%s.unwrap()).as_string();\n"
                                "      if _str_%s.is_some() {\n"
                                "        let _s_%s = String::new(_str_%s.unwrap());\n"
                                "        _f_%s.push(_s_%s); _s_%s.forget();\n"
                                "      }\n"
                                "    }\n"
                                "  }\n"
                                "}\n",
                                fn, fn, fn, fn, fn, fn, fn, fn, fn, fn, fn, fn, fn, fn, fn, fn, fn,
                                fn, fn, fn, fn, fn, fn);
                    }
                    else
                    {
                        // Nested struct: call NestedType::from_json recursively
                        snprintf(assign, sizeof(assign), /* TODO: check buffer size */
                                 "let _opt_%s = (*j).get(\"%s\");\n"
                                 "let _f_%s: %s;\n"
                                 "if _opt_%s.is_some() { _f_%s = "
                                 "%s::from_json(_opt_%s.unwrap()).unwrap(); }\n",
                                 fn, fn, fn, ft, fn, fn, ft, fn);
                    }
                    strncat(body, assign, sizeof(body) - strlen(body) - 1);
                }
                f = f->next;
            }

            // Build struct initialization
            strncat(body, "return Result<", sizeof(body) - strlen(body) - 1);
            strncat(body, name, sizeof(body) - strlen(body) - 1);
            strncat(body, ">::Ok(", sizeof(body) - strlen(body) - 1);
            strncat(body, name, sizeof(body) - strlen(body) - 1);
            strncat(body, " { ", sizeof(body) - strlen(body) - 1);

            f = strct->strct.fields;
            int first = 1;
            while (f)
            {
                if (f->kind == NODE_FIELD)
                {
                    if (!first)
                    {
                        strncat(body, ", ", sizeof(body) - strlen(body) - 1);
                    }
                    char init[128];
                    // Check if this is a Vec<String> field - clone it to avoid double-free
                    int is_vec_field = 0;
                    for (int vi = 0; vi < vec_field_count; vi++)
                    {
                        if (strcmp(vec_fields[vi], f->field.name) == 0)
                        {
                            is_vec_field = 1;
                            break;
                        }
                    }
                    if (is_vec_field)
                    {
                        snprintf(init, 128, "%s: _f_%s.clone()", f->field.name, f->field.name);
                    }
                    else
                    {
                        snprintf(init, 128, "%s: _f_%s", f->field.name, f->field.name);
                    }
                    strncat(body, init, sizeof(body) - strlen(body) - 1);
                    first = 0;
                }
                f = f->next;
            }
            strncat(body, " }); ", sizeof(body) - strlen(body) - 1);

            code = xmalloc(8192 + 1024);
            sprintf(code, "impl %s { fn from_json(j: JsonValue*) -> Result<%s> { %s } }",
                    name, /* safe */
                    name, body);
        }
        else if (0 == strcmp(trait, "ToJson"))
        {
            // Generate to_json(self) -> JsonValue
            // Only works for structs (not enums)
            if (strct->kind != NODE_STRUCT)
            {
                zwarn_at(strct->token, "@derive(ToJson) only works on structs");
                continue;
            }

            char body[8192];
            strcpy(body, "let _obj = JsonValue::object();\n");

            ASTNode *f = strct->strct.fields;
            while (f)
            {
                if (f->kind == NODE_FIELD)
                {
                    char *fn = f->field.name;
                    char *ft = f->field.type;
                    char set_call[2048];

                    if (!fn || !ft)
                    {
                        f = f->next;
                        continue;
                    }

                    int is_int_type = strcmp(ft, "int") == 0 || strcmp(ft, "int32_t") == 0 ||
                                      strcmp(ft, "i32") == 0 || strcmp(ft, "i64") == 0 ||
                                      strcmp(ft, "int64_t") == 0 || strcmp(ft, "u32") == 0 ||
                                      strcmp(ft, "uint32_t") == 0 || strcmp(ft, "u64") == 0 ||
                                      strcmp(ft, "uint64_t") == 0 || strcmp(ft, "usize") == 0 ||
                                      strcmp(ft, "size_t") == 0;
                    if (is_int_type)
                    {
                        snprintf(set_call, 2048,
                                 "_obj.set(\"%s\", JsonValue::number((double)self.%s));\n", fn, fn);
                    }
                    else if (strcmp(ft, "double") == 0)
                    {
                        snprintf(set_call, sizeof(set_call),
                                 "_obj.set(\"%s\", JsonValue::number(self.%s));\n",
                                 fn, /* TODO: check buffer size */
                                 fn);
                    }
                    else if (strcmp(ft, "bool") == 0)
                    {
                        snprintf(set_call, sizeof(set_call),
                                 "_obj.set(\"%s\", JsonValue::bool(self.%s));\n", fn,
                                 fn); /* TODO: check buffer size */
                    }
                    else if (strcmp(ft, "char*") == 0)
                    {
                        snprintf(set_call, sizeof(set_call),
                                 "_obj.set(\"%s\", JsonValue::string(self.%s));\n",
                                 fn, /* TODO: check buffer size */
                                 fn);
                    }
                    else if (strcmp(ft, "String") == 0)
                    {
                        snprintf(
                            set_call, sizeof(set_call),
                            "_obj.set(\"%s\", JsonValue::string(self.%s.c_str()));\n", /* TODO:
                                                                                          check
                                                                                          buffer
                                                                                          size */
                            fn, fn);
                    }
                    else if (ft && strstr(ft, "Vec") && strstr(ft, "String"))
                    {
                        snprintf(
                            set_call, sizeof(set_call), /* TODO: check buffer size */
                            "let _arr_%s = JsonValue::array();\n"
                            "for let _i_%s: usize = 0; _i_%s < self.%s.length(); _i_%s = _i_%s "
                            "+ 1 {\n"
                            "  _arr_%s.push(JsonValue::string(self.%s.get(_i_%s).c_str()));\n"
                            "}\n"
                            "_obj.set(\"%s\", _arr_%s);\n",
                            fn, fn, fn, fn, fn, fn, fn, fn, fn, fn, fn);
                    }
                    else
                    {
                        // Nested struct: call to_json recursively
                        snprintf(set_call, sizeof(set_call),
                                 "_obj.set(\"%s\", self.%s.to_json());\n", fn,
                                 fn); /* TODO: check buffer size */
                    }
                    strncat(body, set_call, sizeof(body) - strlen(body) - 1);
                }
                f = f->next;
            }

            strncat(body, "return _obj;", sizeof(body) - strlen(body) - 1);

            code = xmalloc(8192 + 1024);
            sprintf(code, "impl %s { fn to_json(self) -> JsonValue { %s } }", name,
                    body); /* safe */
        }

        if (code)
        {
            Lexer tmp;
            lexer_init(&tmp, code, ctx->config, ctx->current_filename);
            ASTNode *impl = parse_impl(ctx, &tmp);
            if (impl)
            {
                if (!head)
                {
                    head = impl;
                }
                else
                {
                    tail->next = impl;
                }
                tail = impl;
            }
        }
    }
    return head;
}
