/* ============================================================================
 *  ast.ts
 *  ----------------------------------------------------------------------------
 *  Abstract syntax tree node types for the cliforge schema language.
 *
 *  Every node carries an inclusive `range` (start..end offsets in the source
 *  text) so the LSP can map diagnostics, hovers, completions, and definitions
 *  back to editor positions.
 * ========================================================================== */

export interface Range {
    /** Inclusive start offset (0-based) in the source text. */
    start: number;
    /** Exclusive end offset (0-based) in the source text. */
    end: number;
}

/** Common shape every AST node shares. */
export interface NodeBase {
    range: Range;
    /** Optional doc comment that immediately precedes this node. */
    leadingDoc?: string;
}

export type Node =
    | SchemaFile
    | SchemaDirective
    | ImportDirective
    | MetaBlock
    | SectionBlock
    | OptionDecl
    | GroupDecl
    | SubcommandDecl
    | PositionalDecl
    | NamedTypeDecl
    | ConditionalBlock
    | TypeExpr
    | CompoundTypeExpr
    | ChoiceTypeExpr
    | VariantDefaultBlock
    | AllowedBlock
    | KeyValue
    | Identifier
    | Literal
    | CompoundField;

export interface SchemaFile extends NodeBase {
    kind: "SchemaFile";
    directive?: SchemaDirective;
    imports: ImportDirective[];
    meta?: MetaBlock;
    /** All top-level and section-level named type declarations. */
    namedTypes: NamedTypeDecl[];
    sections: SectionBlock[];
    options: OptionDecl[];           // top-level options (outside any section)
    groups: GroupDecl[];
    subcommands: SubcommandDecl[];
    conditionals: ConditionalBlock[];
    /** Raw children in source order — useful for outline rendering. */
    children: Node[];
}

export interface SchemaDirective extends NodeBase {
    kind: "SchemaDirective";
    language: string;        // expected "cliforge"
    version: string;         // expected "v1"
}

export interface ImportDirective extends NodeBase {
    kind: "ImportDirective";
    path: string;            // string literal contents (no quotes)
    pathRange: Range;
    alias: string | null;    // mandatory but may be missing → diagnostic
    aliasRange?: Range;
    /** Optional conditional guard on the import line. */
    ifkey?: {
        key: string;
        /** True when the guard keyword is `ifnkey` (negate the condition). */
        negated?: boolean;
        op?: "==" | "!=";
        value?: string;
    };
    ifkeyRange?: Range;
}

export interface MetaBlock extends NodeBase {
    kind: "MetaBlock";
    entries: KeyValue[];
}

/**
 * A build-time or key-words conditional block.
 *
 *   ifdef SYMBOL { ... }
 *   ifndef SYMBOL { ... }
 *   ifdef A || B { ... }
 *   ifkey KEY { ... }
 *   ifkey KEY == VALUE { ... }
 *   ifnkey KEY { ... }
 */
export interface ConditionalBlock extends NodeBase {
    kind: "ConditionalBlock";
    /** "ifdef" | "ifndef" | "ifkey" | "ifnkey" */
    keyword: string;
    /** For ifdef/ifndef: space-separated symbol expression (e.g. "LINUX_BUILD || QNX_BUILD"). */
    symbolExpr?: string;
    /** For ifkey/ifnkey: the key name. */
    keyName?: string;
    /** For ifkey/ifnkey with == or !=. */
    op?: "==" | "!=";
    /** The right-hand value when op is present. */
    keyValue?: string;
    /** Contents of the block (sections, options, named type decls, nested conditionals). */
    children: Node[];
}

export interface SectionBlock extends NodeBase {
    kind: "SectionBlock";
    title?: string;
    titleRange?: Range;
    /** Named type declarations defined inside this section for visual proximity. */
    namedTypes: NamedTypeDecl[];
    options: OptionDecl[];
    /** Nested conditional blocks inside this section. */
    conditionals: ConditionalBlock[];
    /** Raw children in source order. */
    children: Node[];
}

export interface OptionDecl extends NodeBase {
    kind: "OptionDecl";
    name: string;
    nameRange: Range;
    qualifiers: KeyValue[];
}

export interface GroupDecl extends NodeBase {
    kind: "GroupDecl";
    name: string;
    nameRange: Range;
    /** The `options = [...]` qualifier. */
    options: Identifier[];
    /** Whether the `mandatory` bare keyword was present (exactly-one enforcement). */
    mandatory: boolean;
}

/**
 * A subcommand block.
 *
 *   subcommand eval {
 *     brief       = "Evaluate one or more expressions"
 *     description = "..."
 *     option echo { ... }
 *     positional expressions { ... }
 *   }
 */
export interface SubcommandDecl extends NodeBase {
    kind: "SubcommandDecl";
    name: string;
    nameRange: Range;
    /** Options declared inside this subcommand. */
    options: OptionDecl[];
    /** Positional arguments declared inside this subcommand. */
    positionals: PositionalDecl[];
    /** Conditional blocks inside this subcommand. */
    conditionals: ConditionalBlock[];
    /** Raw children in source order. */
    children: Node[];
}

/**
 * A positional argument block.
 *
 *   positional expressions {
 *     type     = string(length=1024)
 *     multiple = 1..32
 *     help     = "Expressions to evaluate."
 *   }
 */
export interface PositionalDecl extends NodeBase {
    kind: "PositionalDecl";
    name: string;
    nameRange: Range;
    qualifiers: KeyValue[];
}

/**
 * A named type declaration — either a choice type or a compound type.
 *
 *   verbosity = (quiet, normal, verbose, trace)   // ChoiceTypeExpr
 *   endpoint  = { host: string, port: uint16 }    // CompoundTypeExpr
 *
 * May appear at top-level or inside a section block.
 */
export interface NamedTypeDecl extends NodeBase {
    kind: "NamedTypeDecl";
    name: string;
    nameRange: Range;
    typeExpr: ChoiceTypeExpr | CompoundTypeExpr;
}

export interface KeyValue extends NodeBase {
    kind: "KeyValue";
    key: string;
    keyRange: Range;
    value: ValueExpr;
}

export type ValueExpr =
    | Literal
    | Identifier
    | TypeExpr
    | CompoundTypeExpr
    | ChoiceTypeExpr
    | VariantDefaultBlock
    | AllowedBlock
    | IdentifierList
    | RangeExpr
    | MultipleExpr;

/**
 * A primitive or named-type reference, optionally with constructor args
 * and/or an `in lo..hi` range constraint.
 *
 *   uint16
 *   uint16 in 1..65535
 *   string(length=64)
 *   duration
 */
export interface TypeExpr extends NodeBase {
    kind: "TypeExpr";
    /** e.g. "uint16", "duration", "string", or a named type alias. */
    baseName: string;
    baseRange: Range;
    /** Constructor args, e.g. `(length=32)`. */
    args: TypeArg[];
    /** Inline `in lo..hi` constraint that may follow a type. */
    rangeConstraint?: RangeExpr;
}

export interface TypeArg extends NodeBase {
    kind: "TypeArg";
    /** Bare identifier (e.g. `ns`) or `length=32` style key=value. */
    name: string;
    value?: Literal | Identifier;
}

/**
 * Inline or named compound (struct-like) type.
 *
 *   { host: string, port: uint16 }
 */
export interface CompoundTypeExpr extends NodeBase {
    kind: "CompoundTypeExpr";
    fields: CompoundField[];
}

export interface CompoundField extends NodeBase {
    kind: "CompoundField";
    name: string;
    nameRange: Range;
    fieldType: TypeExpr;
    /** Optional `= default` expression. */
    defaultValue?: ValueExpr;
    /** Inline `in lo..hi` constraint on the field's type. */
    rangeConstraint?: RangeExpr;
    /** Doc comment immediately preceding this field. */
    fieldDoc?: string;
}

/**
 * Inline or named choice (enum-like) type.
 *
 *   (quiet, normal, verbose, trace)
 */
export interface ChoiceTypeExpr extends NodeBase {
    kind: "ChoiceTypeExpr";
    members: Identifier[];
}

/**
 * Variant-keyed default block.
 *
 *   default = {
 *     ifdef LINUX_BUILD : "/var/log/app.log"
 *     ifkey board == rpi4 : "/mnt/log/app.log"
 *     default            : "./app.log"
 *   }
 */
export interface VariantDefaultBlock extends NodeBase {
    kind: "VariantDefaultBlock";
    arms: VariantArm[];
}

export interface VariantArm extends NodeBase {
    kind: "VariantArm";
    /**
     * Condition kind:
     *   "ifdef" | "ifndef" | "ifkey" | "ifnkey" | "default"
     */
    condKind: string;
    /** Symbol or key name (omitted for "default" arm). */
    condName?: string;
    /** Comparison operator for ifkey arms. */
    op?: "==" | "!=";
    /** Right-hand value for ifkey arms with == / !=. */
    condValue?: string;
    value: ValueExpr;
}

/**
 * `allowed { }` block: conditional set of valid choice values.
 *
 *   allowed {
 *     ifdef FULL_PROTO_SUPPORT : (ethernet, ipv4, ipv6, tcp, udp)
 *     default                  : (ipv4, ipv6, tcp, udp)
 *   }
 */
export interface AllowedBlock extends NodeBase {
    kind: "AllowedBlock";
    arms: AllowedArm[];
}

export interface AllowedArm extends NodeBase {
    kind: "AllowedArm";
    condKind: string;   // "ifdef" | "ifndef" | "ifkey" | "ifnkey" | "if" | "default"
    condName?: string;
    op?: "==" | "!=";
    condValue?: string;
    /** The parenthesised list of allowed values for this arm. */
    values: Identifier[];
}

export interface Literal extends NodeBase {
    kind: "Literal";
    /** Source-form text (incl. unit suffix for quantities). */
    raw: string;
    literalKind: "string" | "char" | "number" | "quantity" | "boolean";
    /** Parsed numeric value when applicable. */
    numericValue?: number;
    /** Parsed unit suffix when quantity. */
    unit?: string;
    /** Parsed boolean value. */
    boolValue?: boolean;
    /** Decoded string contents (without quotes). */
    stringValue?: string;
}

export interface Identifier extends NodeBase {
    kind: "Identifier";
    name: string;
}

export interface IdentifierList extends NodeBase {
    kind: "IdentifierList";
    items: Identifier[];
}

export interface RangeExpr extends NodeBase {
    kind: "RangeExpr";
    low: Literal;
    high: Literal;
}

/**
 * `multiple` qualifier value.
 *
 *   multiple = 8        → exact capacity
 *   multiple = 1..32    → min..max occurrence range
 */
export interface MultipleExpr extends NodeBase {
    kind: "MultipleExpr";
    /** Maximum capacity (for bare `N` form and upper bound of range form). */
    max: Literal;
    /** Minimum occurrences (for `min..max` form only). */
    min?: Literal;
}
