/* ============================================================================
 *  validator.ts
 *  ----------------------------------------------------------------------------
 *  Semantic validation pass over a parsed cliforge schema.
 *
 *  Validates: missing @schema directive, reserved-keyword identifiers,
 *  duplicate option/type names, missing default-arm in variant blocks,
 *  unknown qualifiers, unit-group mismatches, group integrity, etc.
 * ========================================================================== */

import {
    SchemaFile, OptionDecl, GroupDecl, NamedTypeDecl, ImportDirective,
    SectionBlock, KeyValue, TypeExpr, CompoundTypeExpr, Literal,
    VariantDefaultBlock, RangeExpr,
} from "./ast";
import { ParseDiagnostic } from "./parser";
import {
    RESERVED_KEYWORDS, FIELD_RESERVED_KEYWORDS, QUALIFIER_KEYS, META_KEYS,
    PRIMITIVE_TYPES, QUANTITY_TYPES, UNIT_GROUP,
    BOOLEAN_LITERALS, REQUIRED_VALUES, VISIBLE_VALUES,
} from "./keywords";

/** Lookup table built once per file to resolve cross-references. */
interface SymbolTable {
    optionsByName: Map<string, OptionDecl>;
    namedTypesByName: Map<string, NamedTypeDecl>;
    importAliases: Map<string, ImportDirective>;
}

export function validate(file: SchemaFile): ParseDiagnostic[] {
    const diags: ParseDiagnostic[] = [];
    const table = buildSymbolTable(file, diags);

    /* ------------ schema directive ------------ */
    if (!file.directive) {
        diags.push({
            range: { start: 0, end: 0 },
            message: "Schema must begin with `@schema cliforge v1`",
            severity: "error",
            code: "missing-schema-directive",
        });
    } else {
        if (file.directive.language && file.directive.language !== "cliforge") {
            diags.push({
                range: file.directive.range,
                message: `Expected schema language 'cliforge', got '${file.directive.language}'`,
                severity: "error",
                code: "wrong-schema-language",
            });
        }
        if (file.directive.version && !/^v\d+$/.test(file.directive.version)) {
            diags.push({
                range: file.directive.range,
                message: `Schema version must look like 'v1', got '${file.directive.version}'`,
                severity: "error",
                code: "bad-schema-version",
            });
        } else if (file.directive.version && file.directive.version !== "v1") {
            diags.push({
                range: file.directive.range,
                message: `This extension only understands schema version 'v1' (got '${file.directive.version}')`,
                severity: "warning",
                code: "unknown-schema-version",
            });
        }
    }

    /* ------------ meta block ------------ */
    if (file.meta) {
        for (const e of file.meta.entries) {
            if (!META_KEYS.has(e.key)) {
                diags.push({
                    range: e.keyRange,
                    message: `Unknown meta key '${e.key}'. Expected one of: ${[...META_KEYS].join(", ")}`,
                    severity: "warning",
                    code: "unknown-meta-key",
                });
            }
        }
        const hasApp = file.meta.entries.some(e => e.key === "app");
        if (!hasApp) {
            diags.push({
                range: file.meta.range,
                message: "`meta` block must declare `app = \"<name>\"`",
                severity: "error",
                code: "missing-meta-app",
            });
        }
    }

    /* ------------ imports ------------ */
    for (const imp of file.imports) {
        if (!imp.alias) {
            diags.push({
                range: imp.range,
                message: "@import requires a mandatory 'as <alias>' clause",
                severity: "error",
                code: "missing-import-alias",
            });
        } else if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(imp.alias)) {
            diags.push({
                range: imp.aliasRange ?? imp.range,
                message: `Import alias '${imp.alias}' is not a valid C identifier`,
                severity: "error",
                code: "bad-import-alias",
            });
        } else if (RESERVED_KEYWORDS.has(imp.alias)) {
            diags.push({
                range: imp.aliasRange ?? imp.range,
                message: `Import alias '${imp.alias}' is a reserved keyword`,
                severity: "error",
                code: "reserved-import-alias",
            });
        }
    }

    /* ------------ options (top-level + per-section) ------------ */
    for (const o of file.options) validateOption(o, table, diags);
    for (const sec of file.sections) {
        validateSection(sec, diags);
        for (const o of sec.options) validateOption(o, table, diags);
        for (const nt of sec.namedTypes) validateNamedType(nt, diags);
    }

    /* ------------ top-level named types ------------ */
    for (const nt of file.namedTypes) validateNamedType(nt, diags);

    /* ------------ groups ------------ */
    for (const g of file.groups) validateGroup(g, table, diags);

    return diags;
}

/* ---------------------------------------------------------------------------- */

function buildSymbolTable(file: SchemaFile, diags: ParseDiagnostic[]): SymbolTable {
    const optionsByName = new Map<string, OptionDecl>();
    const namedTypesByName = new Map<string, NamedTypeDecl>();
    const importAliases = new Map<string, ImportDirective>();

    const allOptions: OptionDecl[] = [
        ...file.options,
        ...file.sections.flatMap(s => s.options),
    ];
    for (const o of allOptions) {
        if (optionsByName.has(o.name)) {
            diags.push({
                range: o.nameRange,
                message: `Duplicate option name '${o.name}'`,
                severity: "error",
                code: "duplicate-option",
            });
        } else {
            optionsByName.set(o.name, o);
        }
    }

    const allNamedTypes: NamedTypeDecl[] = [
        ...file.namedTypes,
        ...file.sections.flatMap(s => s.namedTypes),
    ];
    for (const nt of allNamedTypes) {
        if (namedTypesByName.has(nt.name)) {
            diags.push({
                range: nt.nameRange,
                message: `Duplicate type name '${nt.name}'`,
                severity: "error",
                code: "duplicate-type",
            });
        } else {
            namedTypesByName.set(nt.name, nt);
        }
    }

    for (const i of file.imports) {
        if (!i.alias) continue;
        if (importAliases.has(i.alias)) {
            diags.push({
                range: i.aliasRange ?? i.range,
                message: `Duplicate import alias '${i.alias}'`,
                severity: "error",
                code: "duplicate-import-alias",
            });
        } else {
            importAliases.set(i.alias, i);
        }
    }
    return { optionsByName, namedTypesByName, importAliases };
}

/* ---------------------------------------------------------------------------- */

function validateSection(sec: SectionBlock, diags: ParseDiagnostic[]): void {
    // Reserved hook for future section-level validations.
    void sec; void diags;
}

function validateOption(o: OptionDecl, table: SymbolTable, diags: ParseDiagnostic[]): void {
    if (RESERVED_KEYWORDS.has(o.name)) {
        diags.push({
            range: o.nameRange,
            message: `'${o.name}' is a reserved keyword and cannot be used as an option name`,
            severity: "error",
            code: "reserved-option-name",
        });
    }

    let hasDefault = false;
    let isMandatory = false;
    let typeQualifier: KeyValue | undefined;
    const seenKeys = new Set<string>();

    for (const q of o.qualifiers) {
        if (seenKeys.has(q.key)) {
            diags.push({
                range: q.keyRange,
                message: `Duplicate qualifier '${q.key}' on option '${o.name}'`,
                severity: "error",
                code: "duplicate-qualifier",
            });
        }
        seenKeys.add(q.key);

        if (!QUALIFIER_KEYS.has(q.key)) {
            diags.push({
                range: q.keyRange,
                message: `Unknown qualifier '${q.key}'. Expected one of: ${[...QUALIFIER_KEYS].sort().join(", ")}`,
                severity: "warning",
                code: "unknown-qualifier",
            });
        }

        if (q.key === "default") hasDefault = true;

        if (q.key === "required" && q.value.kind === "Identifier") {
            if (!REQUIRED_VALUES.has(q.value.name)) {
                diags.push({
                    range: q.value.range,
                    message: `'required' must be 'optional' or 'mandatory', got '${q.value.name}'`,
                    severity: "error",
                    code: "bad-required-value",
                });
            }
            if (q.value.name === "mandatory") isMandatory = true;
        }

        if (q.key === "visible" && q.value.kind === "Identifier") {
            if (!VISIBLE_VALUES.has(q.value.name)) {
                diags.push({
                    range: q.value.range,
                    message: `'visible' must be 'all', 'detail', or 'never', got '${q.value.name}'`,
                    severity: "error",
                    code: "bad-visible-value",
                });
            }
        }

        if (q.key === "short" && q.value.kind === "Literal" && q.value.literalKind !== "char") {
            diags.push({
                range: q.value.range,
                message: "`short` must be a character literal, e.g. short = 'v'",
                severity: "error",
                code: "bad-short",
            });
        }

        if (q.key === "default" && q.value.kind === "VariantDefaultBlock") {
            checkVariantDefault(q.value, diags);
        }

        if (q.key === "type") typeQualifier = q;
    }

    if (hasDefault && isMandatory) {
        diags.push({
            range: o.range,
            message: "`required = mandatory` and `default = ...` are mutually exclusive",
            severity: "error",
            code: "required-and-default",
        });
    }

    if (typeQualifier) checkTypeAndDefault(o, typeQualifier, diags, table);
}

function checkVariantDefault(v: VariantDefaultBlock, diags: ParseDiagnostic[]): void {
    const hasDefaultArm = v.arms.some(a => a.condKind === "default");
    if (!hasDefaultArm) {
        diags.push({
            range: v.range,
            message: "Variant-keyed default block must include a `default:` fallback arm",
            severity: "error",
            code: "missing-default-arm",
        });
    }
    const seen = new Set<string>();
    for (const a of v.arms) {
        const key = a.condKind + (a.condName ? ` ${a.condName}` : "") + (a.condValue ? `==${a.condValue}` : "");
        if (seen.has(key)) {
            diags.push({
                range: a.range,
                message: `Duplicate variant arm '${key}'`,
                severity: "error",
                code: "duplicate-variant-arm",
            });
        }
        seen.add(key);
    }
}

function checkTypeAndDefault(
    o: OptionDecl,
    typeKv: KeyValue,
    diags: ParseDiagnostic[],
    table: SymbolTable,
): void {
    const tv = typeKv.value;

    if (tv.kind === "CompoundTypeExpr") {
        checkCompoundFields(tv, table, diags);
        return;
    }
    if (tv.kind === "ChoiceTypeExpr") {
        // choice types — no further checks needed at this stage
        return;
    }
    if (tv.kind !== "TypeExpr") return;

    const base = tv.baseName;

    // Type name must be a known primitive, quantity, or named type
    if (!PRIMITIVE_TYPES.has(base) && !QUANTITY_TYPES.has(base) &&
        !table.namedTypesByName.has(base)) {
        diags.push({
            range: tv.baseRange,
            message: `Unknown type '${base}'. Expected a primitive, a quantity type, or a defined named type.`,
            severity: "error",
            code: "unknown-type",
        });
    }

    // Quantity types: warn if no unit arg supplied and no display-unit qualifier present
    if (QUANTITY_TYPES.has(base)) {
        const hasDisplayUnit = o.qualifiers.some(q => q.key === "display-unit");
        if (tv.args.length === 0 && !hasDisplayUnit) {
            diags.push({
                range: tv.range,
                message: `${base} requires a display hint (e.g. display-unit = "ms") for documentation`,
                severity: "warning",
                code: "missing-quantity-display-unit",
            });
        } else if (base !== "ratio") {
            const targetArg = tv.args[0];
            const targetUnit = targetArg.name;
            const group = UNIT_GROUP[targetUnit];
            if (group !== base) {
                diags.push({
                    range: targetArg.range,
                    message: `'${targetUnit}' is not a valid unit hint for ${base}(...)`,
                    severity: "error",
                    code: "bad-quantity-target",
                });
            }
        }
    }

    // Warn on mixing decimal/binary byte units in default vs type hint
    if (base === "bytes" && tv.args.length > 0) {
        const target = tv.args[0].name;
        const def = o.qualifiers.find(q => q.key === "default");
        if (def && def.value.kind === "Literal" && def.value.literalKind === "quantity") {
            const u = def.value.unit;
            if (u && isDecimalByteUnit(target) !== isDecimalByteUnit(u)) {
                diags.push({
                    range: def.value.range,
                    message: `Mixing decimal (${u}) and binary (${target}) byte units — clarify to avoid ambiguity.`,
                    severity: "warning",
                    code: "bytes-decimal-binary-mix",
                });
            }
        }
    }

    // Default value type compatibility
    const def = o.qualifiers.find(q => q.key === "default");
    if (def && def.value.kind === "Literal") {
        checkLiteralAgainstType(def.value, base, tv, table, diags);
    }

    // Range constraint compatibility
    if (tv.rangeConstraint) {
        checkRangeAgainstType(tv.rangeConstraint, base, diags);
    }
}

function isDecimalByteUnit(u: string): boolean {
    return u === "KB" || u === "MB" || u === "GB" || u === "TB" || u === "B";
}

function checkLiteralAgainstType(
    lit: Literal,
    typeBase: string,
    tv: TypeExpr,
    table: SymbolTable,
    diags: ParseDiagnostic[],
): void {
    if (typeBase === "bool" || typeBase === "flag") {
        if (lit.literalKind !== "boolean") {
            diags.push({
                range: lit.range,
                message: `Default for '${typeBase}' must be a boolean literal (true / false)`,
                severity: "error",
                code: "bad-default-type",
            });
        }
        return;
    }
    if (typeBase === "string" || typeBase === "path" || typeBase === "file" || typeBase === "dir") {
        if (lit.literalKind !== "string") {
            diags.push({
                range: lit.range,
                message: `Default for '${typeBase}' must be a string literal`,
                severity: "error",
                code: "bad-default-type",
            });
        }
        return;
    }
    if (PRIMITIVE_TYPES.has(typeBase) &&
        !["string", "path", "file", "dir", "bool", "flag"].includes(typeBase)) {
        if (lit.literalKind !== "number" && lit.literalKind !== "quantity") {
            diags.push({
                range: lit.range,
                message: `Default for '${typeBase}' must be a numeric literal`,
                severity: "error",
                code: "bad-default-type",
            });
        }
        return;
    }
    if (QUANTITY_TYPES.has(typeBase) && lit.literalKind === "quantity" && lit.unit) {
        if (typeBase !== "ratio" && UNIT_GROUP[lit.unit] !== typeBase) {
            diags.push({
                range: lit.range,
                message: `Default unit '${lit.unit}' is incompatible with ${typeBase}(...)`,
                severity: "error",
                code: "incompatible-default-unit",
            });
        }
    }
    // Named types (choice) — default should be a string matching one of the members
    if (table.namedTypesByName.has(typeBase) && lit.literalKind !== "string") {
        diags.push({
            range: lit.range,
            message: `Default for named choice type '${typeBase}' must be a quoted string`,
            severity: "warning",
            code: "choice-default-not-string",
        });
    }
    void tv; // reserved
}

function checkRangeAgainstType(r: RangeExpr, typeBase: string, diags: ParseDiagnostic[]): void {
    if (PRIMITIVE_TYPES.has(typeBase) &&
        !["string", "path", "file", "dir", "bool", "flag"].includes(typeBase)) {
        if (r.low.numericValue !== undefined && r.high.numericValue !== undefined &&
            r.low.numericValue > r.high.numericValue) {
            diags.push({
                range: r.range,
                message: "Range lower bound is greater than upper bound",
                severity: "error",
                code: "inverted-range",
            });
        }
    }
}

function checkCompoundFields(s: CompoundTypeExpr, table: SymbolTable, diags: ParseDiagnostic[]): void {
    const seen = new Set<string>();
    for (const f of s.fields) {
        if (seen.has(f.name)) {
            diags.push({
                range: f.nameRange,
                message: `Duplicate field '${f.name}' in compound type`,
                severity: "error",
                code: "duplicate-compound-field",
            });
        }
        seen.add(f.name);
        if (FIELD_RESERVED_KEYWORDS.has(f.name)) {
            diags.push({
                range: f.nameRange,
                message: `'${f.name}' is a reserved keyword and cannot be used as a field name`,
                severity: "error",
                code: "reserved-field-name",
            });
        }
        const ft = f.fieldType;
        if (!PRIMITIVE_TYPES.has(ft.baseName) && !QUANTITY_TYPES.has(ft.baseName) &&
            !table.namedTypesByName.has(ft.baseName)) {
            diags.push({
                range: ft.baseRange,
                message: `Unknown type '${ft.baseName}' on field '${f.name}'`,
                severity: "error",
                code: "unknown-field-type",
            });
        }
    }
}

function validateGroup(g: GroupDecl, table: SymbolTable, diags: ParseDiagnostic[]): void {
    if (g.options.length === 0) {
        diags.push({
            range: g.range,
            message: `group '${g.name}' must declare 'options = [...]'`,
            severity: "error",
            code: "missing-group-options",
        });
        return;
    }
    for (const ref of g.options) {
        if (!table.optionsByName.has(ref.name)) {
            diags.push({
                range: ref.range,
                message: `Option '${ref.name}' referenced by group '${g.name}' is not defined`,
                severity: "error",
                code: "unknown-group-option",
            });
        }
    }
}

function validateNamedType(nt: NamedTypeDecl, diags: ParseDiagnostic[]): void {
    if (RESERVED_KEYWORDS.has(nt.name)) {
        diags.push({
            range: nt.nameRange,
            message: `'${nt.name}' is a reserved keyword and cannot be used as a type name`,
            severity: "error",
            code: "reserved-type-name",
        });
    }
    if (nt.typeExpr.kind === "ChoiceTypeExpr") {
        const seen = new Set<string>();
        for (const m of nt.typeExpr.members) {
            if (BOOLEAN_LITERALS.has(m.name)) {
                diags.push({
                    range: m.range,
                    message: `Choice member '${m.name}' shadows a boolean literal`,
                    severity: "warning",
                    code: "choice-member-shadows-bool",
                });
            }
            if (seen.has(m.name)) {
                diags.push({
                    range: m.range,
                    message: `Duplicate member '${m.name}' in choice type '${nt.name}'`,
                    severity: "error",
                    code: "duplicate-choice-member",
                });
            }
            seen.add(m.name);
        }
    }
}
