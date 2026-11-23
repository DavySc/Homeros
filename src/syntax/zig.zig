//! Zig's colours, folds and zig fmt from Zig's own parser (std.zig), so a
//! file parses exactly as the compiler of its release parses it.  Built once
//! per Zig release (zig.nix), each copy's C functions named by its release:
//! zig17_parse and so on.  The colours are nvim-treesitter's highlights.scm
//! for Zig, the folds its folds.scm, read off std.zig's tree (measured on
//! Zig 0.17's std against Neovim 0.12: research/zig.md, prototype C).
const std = @import("std");
const Ast = std.zig.Ast;
const T = std.zig.Token.Tag;
const gpa = std.heap.c_allocator;

/// highlights.scm's captures; zig_name gives each one's name
const C = enum(u8) {
    none, comment, comment_documentation, keyword, variable, type, operator, function_builtin,
    punctuation_bracket, punctuation_delimiter, module, keyword_import, string, variable_member,
    type_builtin, keyword_modifier, keyword_conditional, constant, keyword_type, keyword_function,
    keyword_coroutine, variable_parameter, number, label, constant_builtin, keyword_repeat,
    function_call, variable_builtin, function, keyword_return, keyword_exception, keyword_operator,
    character, boolean, string_escape, number_float,
};

/// One parse of the caller's text, which must outlive it: the tree, the
/// fold spans, zig fmt's text.
const H = struct {
    ast: Ast,
    folds: std.ArrayListUnmanaged(u32) = .empty,
    fmt: ?[]u8 = null,
};

/// src[len] is 0.  With face, a capture per byte into it and the folds too;
/// without, the tree alone (for zig fmt).
fn parse(src: [*:0]const u8, len: usize, face: ?[*]u8) callconv(.c) ?*H {
    const h = gpa.create(H) catch return null;
    // Ast.parse takes options from 0.17, a mode before
    const ast = (if (@hasDecl(Ast, "ParseOptions")) Ast.parse(gpa, src[0..len :0], .{}) else Ast.parse(gpa, src[0..len :0], .zig)) catch {
        gpa.destroy(h);
        return null;
    };
    h.* = .{ .ast = ast };
    if (face) |f| {
        const bad = broken(&h.ast) orelse {
            free(h);
            return null;
        };
        defer gpa.free(bad);
        if (!colour(&h.ast, bad, f) or !folds(h, bad)) {
            free(h);
            return null;
        }
    }
    return h;
}

fn free(h: *H) callconv(.c) void {
    if (h.fmt) |f| gpa.free(f);
    h.folds.deinit(gpa);
    h.ast.deinit(gpa);
    gpa.destroy(h);
}

fn errors(h: *H) callconv(.c) usize {
    return h.ast.errors.len;
}

/// the fold spans, two byte offsets each, in tree-sitter's match order
fn foldSpans(h: *H, n: *usize) callconv(.c) [*]const u32 {
    n.* = h.folds.items.len / 2;
    return h.folds.items.ptr;
}

/// zig fmt's text, null where zig fmt refuses (a parse error) or no memory
fn fmt(h: *H, n: *usize) callconv(.c) ?[*]const u8 {
    if (h.ast.errors.len != 0) return null;
    if (h.fmt == null) h.fmt = (if (@hasDecl(Ast, "renderAlloc")) h.ast.renderAlloc(gpa) else h.ast.render(gpa)) catch return null;
    n.* = h.fmt.?.len;
    return h.fmt.?.ptr;
}

fn name(c: u8) callconv(.c) [*:0]const u8 {
    inline for (comptime std.enums.values(C)) |v| if (@intFromEnum(v) == c) return @tagName(v);
    return "";
}

/// the C functions, named for the release that builds this copy (zig.nix
/// writes the root file that calls this)
pub fn exportAs(comptime p: []const u8) void {
    @export(&parse, .{ .name = p ++ "parse" });
    @export(&free, .{ .name = p ++ "free" });
    @export(&errors, .{ .name = p ++ "errors" });
    @export(&foldSpans, .{ .name = p ++ "folds" });
    @export(&fmt, .{ .name = p ++ "fmt" });
    @export(&name, .{ .name = p ++ "name" });
}

fn tokenColour(t: T) C {
    return switch (t) {
        .keyword_asm, .keyword_defer, .keyword_errdefer, .keyword_test, .keyword_error, .keyword_const, .keyword_var => .keyword,
        .keyword_struct, .keyword_union, .keyword_enum, .keyword_opaque => .keyword_type,
        .keyword_suspend, .keyword_nosuspend, .keyword_resume => .keyword_coroutine,
        .keyword_fn => .keyword_function,
        .keyword_and, .keyword_or, .keyword_orelse => .keyword_operator,
        .keyword_return => .keyword_return,
        .keyword_if, .keyword_else, .keyword_switch => .keyword_conditional,
        .keyword_for, .keyword_while, .keyword_break, .keyword_continue => .keyword_repeat,
        .keyword_export => .keyword_import,
        .keyword_try, .keyword_catch => .keyword_exception,
        .keyword_volatile, .keyword_allowzero, .keyword_noalias, .keyword_addrspace, .keyword_align, .keyword_callconv, .keyword_linksection, .keyword_pub, .keyword_inline, .keyword_noinline, .keyword_extern, .keyword_comptime, .keyword_packed, .keyword_threadlocal => .keyword_modifier,
        .keyword_unreachable => .constant_builtin,
        .keyword_anyframe, .keyword_anytype => .type_builtin,
        .equal, .asterisk_equal, .asterisk_percent_equal, .asterisk_pipe_equal, .slash_equal, .percent_equal, .plus_equal, .plus_percent_equal, .plus_pipe_equal, .minus_equal, .minus_percent_equal, .minus_pipe_equal, .angle_bracket_angle_bracket_left_equal, .angle_bracket_angle_bracket_left_pipe_equal, .angle_bracket_angle_bracket_right_equal, .ampersand_equal, .caret_equal, .pipe_equal, .bang, .tilde, .minus, .minus_percent, .ampersand, .equal_equal, .bang_equal, .angle_bracket_right, .angle_bracket_right_equal, .angle_bracket_left_equal, .angle_bracket_left, .caret, .pipe, .angle_bracket_angle_bracket_left, .angle_bracket_angle_bracket_right, .angle_bracket_angle_bracket_left_pipe, .plus, .plus_plus, .plus_percent, .plus_pipe, .minus_pipe, .asterisk, .slash, .percent, .asterisk_percent, .asterisk_pipe, .pipe_pipe, .period_asterisk, .question_mark, .ellipsis2 => .operator,
        .ellipsis3 => .variable_builtin,
        .l_bracket, .r_bracket, .l_paren, .r_paren, .l_brace, .r_brace => .punctuation_bracket,
        .semicolon, .period, .comma, .colon, .equal_angle_bracket_right, .arrow => .punctuation_delimiter,
        .char_literal => .character,
        .string_literal, .multiline_string_literal_line => .string,
        .builtin => .function_builtin,
        .doc_comment => .comment,
        .container_doc_comment => .comment_documentation,
        .identifier => .variable,
        else => .none,
    };
}

fn upper(ch: u8) bool {
    return ch >= 'A' and ch <= 'Z';
}
// highlights.scm's #lua-match? "^[A-Z_][a-zA-Z0-9_]*"
fn isType(n: []const u8) bool {
    return n.len > 0 and (upper(n[0]) or n[0] == '_');
}
// and "^[A-Z][A-Z_0-9]+$"
fn isConst(n: []const u8) bool {
    if (n.len < 2 or !upper(n[0])) return false;
    for (n[1..]) |ch| if (!(upper(ch) or ch == '_' or (ch >= '0' and ch <= '9'))) return false;
    return true;
}
fn eql(a: []const u8, b: []const u8) bool {
    return std.mem.eql(u8, a, b);
}
fn isImport(b: []const u8) bool {
    return eql(b, "@import") or eql(b, "@cImport");
}

// An identifier's roles, in highlights.scm's pattern order: the later
// pattern's capture wins, as Neovim's highlighter draws equal priorities
const R = enum(u5) { variable, param, param_type, type_name, container_name, constant_name, enum_literal, enum_field, label, init_field, member, field_name, call, function, module, underscore, cc };
fn bit(r: R) u32 {
    return @as(u32, 1) << @intFromEnum(r);
}

/// Per token, whether it lies in a top-level member the parse found an error
/// in (or right after one: "expected ';'" names the next member's first
/// token).  Such a member's nodes can be unfinished, where std's lastToken
/// unwraps a null (fuzzed: an fn_proto without its return type), so its
/// tokens get their token colours only and it has no folds, until it parses.
fn broken(a: *const Ast) ?[]bool {
    const bad = gpa.alloc(bool, a.tokens.len) catch return null;
    @memset(bad, false);
    const decls = a.rootDecls();
    for (a.errors) |e| {
        var i: usize = 0; // the member holding e.token, or the one before it
        while (i + 1 < decls.len and a.firstToken(decls[i + 1]) <= e.token) i += 1;
        const from = if (i > 0 and a.firstToken(decls[i]) == e.token) i - 1 else i;
        const lo = if (from == 0) 0 else a.firstToken(decls[from]);
        const hi = if (i + 1 < decls.len) a.firstToken(decls[i + 1]) else a.tokens.len;
        @memset(bad[lo..hi], true);
    }
    return bad;
}

fn colour(a: *const Ast, bad: []const bool, out: [*]u8) bool {
    const len = a.source.len;
    @memset(out[0..len], 0);
    const nt = a.tokens.len;
    const role = gpa.alloc(u32, nt) catch return false;
    defer gpa.free(role);
    @memset(role, 0);
    const pipes = gpa.alloc(bool, nt) catch return false; // a payload's |: punctuation.bracket
    defer gpa.free(pipes);
    @memset(pipes, false);
    var buf1: [1]Ast.Node.Index = undefined;
    var buf2: [2]Ast.Node.Index = undefined;
    for (0..a.nodes.len) |k| {
        const n: Ast.Node.Index = @enumFromInt(k);
        if (bad[a.nodeMainToken(n)]) continue;
        if (a.fullFnProto(&buf1, n)) |fp| {
            if (fp.name_token) |nm| role[nm] |= bit(.function);
            var it = fp.iterate(a);
            while (it.next()) |p| {
                if (p.name_token) |nm| role[nm] |= bit(.param);
                if (p.type_expr) |te| if (a.nodeTag(te) == .identifier) {
                    role[a.nodeMainToken(te)] |= bit(.param_type);
                };
            }
        } else if (a.fullVarDecl(n)) |v| {
            const nm = v.ast.mut_token + 1;
            if (v.ast.init_node.unwrap()) |init| {
                if (a.fullContainerDecl(&buf2, init) != null) role[nm] |= bit(.container_name);
                switch (a.nodeTag(init)) {
                    .builtin_call_two, .builtin_call_two_comma, .builtin_call, .builtin_call_comma => if (isImport(a.tokenSlice(a.nodeMainToken(init)))) {
                        role[nm] |= bit(.module);
                    },
                    else => {},
                }
            }
        } else if (a.fullCall(&buf1, n)) |c| {
            const f = c.ast.fn_expr;
            switch (a.nodeTag(f)) {
                .identifier, .enum_literal => role[a.nodeMainToken(f)] |= bit(.call),
                .field_access => role[a.lastToken(f)] |= bit(.call),
                else => {},
            }
        } else if (a.fullStructInit(&buf2, n)) |si| {
            for (si.ast.fields) |fe| role[a.firstToken(fe) - 2] |= bit(.init_field);
        } else if (a.fullContainerDecl(&buf2, n)) |cd| {
            const kw = a.tokenTag(cd.ast.main_token);
            for (cd.ast.members) |m| if (a.fullContainerField(m)) |cf| {
                // (enum_declaration (container_field type: (identifier) @constant));
                // only a struct's fields without a name are tuple types (AstGen)
                if (kw == .keyword_enum and cf.ast.type_expr == .none) {
                    role[cf.ast.main_token] |= bit(.enum_field);
                } else if (!(kw == .keyword_struct and cf.ast.tuple_like)) role[cf.ast.main_token] |= bit(.field_name);
            };
        } else if (a.fullIf(n)) |x| {
            payload(a, role, pipes, x.payload_token);
            payload(a, role, pipes, x.error_token);
        } else if (a.fullWhile(n)) |x| {
            payload(a, role, pipes, x.payload_token);
            payload(a, role, pipes, x.error_token);
        } else if (a.fullFor(n)) |x| {
            payload(a, role, pipes, x.payload_token);
        } else if (a.fullSwitchCase(n)) |x| {
            payload(a, role, pipes, x.payload_token);
        } else switch (a.nodeTag(n)) {
            .@"catch" => if (a.tokenTag(a.nodeMainToken(n) + 1) == .pipe) payload(a, role, pipes, a.nodeMainToken(n) + 2),
            .field_access => role[a.lastToken(n)] |= bit(.member),
            .enum_literal => role[a.nodeMainToken(n)] |= bit(.enum_literal),
            else => {},
        }
    }
    // labels by their neighbours: x: { and x: while, break :x
    for (0..nt) |ti| {
        const t: u32 = @intCast(ti);
        const tg = a.tokenTag(t);
        if (tg == .identifier and t + 2 < nt and a.tokenTag(t + 1) == .colon) switch (a.tokenTag(t + 2)) {
            .l_brace, .keyword_while, .keyword_for, .keyword_inline, .keyword_switch => role[t] |= bit(.label),
            else => {},
        };
        if (tg == .colon and t > 0 and t + 1 < nt and a.tokenTag(t + 1) == .identifier) switch (a.tokenTag(t - 1)) {
            .keyword_break, .keyword_continue => role[t + 1] |= bit(.label),
            else => {},
        };
        // (calling_convention (identifier) @variable.builtin)
        if (tg == .keyword_callconv and t + 2 < nt and a.tokenTag(t + 1) == .l_paren and a.tokenTag(t + 2) == .identifier) role[t + 2] |= bit(.cc);
    }
    for (0..nt) |ti| {
        const k: u32 = @intCast(ti);
        const tg = a.tokenTag(k);
        if (tg == .eof) break;
        const s = a.tokenStart(k);
        const txt = a.tokenSlice(k);
        var c = tokenColour(tg);
        if (tg == .identifier) {
            // tree-sitter's own nodes for these words, except where a name
            // stands (.true, x.bool, a field called null)
            const plain = role[k] & ~bit(.param_type) == 0;
            if (plain and (eql(txt, "true") or eql(txt, "false"))) {
                c = .boolean;
            } else if (plain and (eql(txt, "null") or eql(txt, "undefined"))) {
                c = .constant_builtin;
            } else if (plain and std.zig.primitives.isPrimitive(txt)) {
                c = .type_builtin;
            } else {
                var rs = role[k];
                if (isType(txt)) rs |= bit(.type_name);
                if (isConst(txt)) rs |= bit(.constant_name);
                if (eql(txt, "_")) rs |= bit(.underscore);
                if (eql(txt, "c") and k >= 2 and a.tokenTag(k - 1) == .asterisk and a.tokenTag(k - 2) == .l_bracket) rs |= bit(.cc); // [*c]
                const best: R = @enumFromInt(31 - @clz(rs | 1));
                c = switch (best) {
                    .variable => .variable,
                    .param => .variable_parameter,
                    .param_type, .type_name, .container_name => .type,
                    .constant_name, .enum_literal, .enum_field => .constant,
                    .label => .label,
                    .init_field, .member, .field_name => .variable_member,
                    .call => .function_call,
                    .function => .function,
                    .module => .module,
                    .underscore, .cc => .variable_builtin,
                };
            }
        } else if (tg == .number_literal) {
            const hex = txt.len > 1 and txt[0] == '0' and (txt[1] == 'x' or txt[1] == 'X');
            c = .number;
            for (txt) |ch| if (ch == '.' or (!hex and (ch == 'e' or ch == 'E')) or (hex and (ch == 'p' or ch == 'P'))) {
                c = .number_float;
            };
        } else if (tg == .builtin and isImport(txt) and k >= 2 and a.tokenTag(k - 1) == .equal and (role[k - 2] & bit(.module) != 0 or eql(a.tokenSlice(k - 2), "_"))) {
            c = .keyword_import;
        } else if ((tg == .period and k + 1 < nt and a.tokenTag(k + 1) == .question_mark and a.tokenStart(k + 1) == s + 1) or
            (tg == .question_mark and a.tokenTag(k - 1) == .period and a.tokenStart(k - 1) + 1 == s))
        {
            c = .operator; // ".?" is one token in tree-sitter
        }
        if (pipes[k]) c = .punctuation_bracket;
        @memset(out[s .. s + txt.len], @intFromEnum(c));
        if (tg == .string_literal or tg == .char_literal) escapes(txt, out + s);
    }
    // comments, which std.zig's tokenizer skips (but for doc comments)
    var i: usize = 0;
    var tk: u32 = 0;
    while (i + 1 < len) {
        while (tk < nt and a.tokenStart(tk) + a.tokenSlice(tk).len <= i) tk += 1;
        if (tk < nt and a.tokenStart(tk) <= i and a.tokenTag(tk) != .eof) {
            i = a.tokenStart(tk) + a.tokenSlice(tk).len;
        } else if (a.source[i] == '/' and a.source[i + 1] == '/') {
            const e = std.mem.indexOfScalarPos(u8, a.source, i, '\n') orelse len;
            @memset(out[i..e], @intFromEnum(C.comment));
            i = e;
        } else i += 1;
    }
    return true;
}

/// (escape_sequence) @string.escape inside a string or char literal
fn escapes(txt: []const u8, o: [*]u8) void {
    var i: usize = 1;
    while (i + 1 < txt.len) : (i += 1) if (txt[i] == '\\') {
        var e = i + 2;
        if (txt[i + 1] == 'x') e = i + 4 else if (txt[i + 1] == 'u') {
            while (e < txt.len and txt[e - 1] != '}') e += 1;
        }
        @memset(o[i..@min(e, txt.len - 1)], @intFromEnum(C.string_escape));
        i = e - 1;
    };
}

/// (payload (identifier) @variable.parameter) and its "|" @punctuation.bracket
fn payload(a: *const Ast, role: []u32, pipes: []bool, first: ?Ast.TokenIndex) void {
    var t = first orelse return;
    while (t > 0 and a.tokenTag(t) != .pipe) t -= 1;
    pipes[t] = true;
    t += 1;
    while (t < a.tokens.len and a.tokenTag(t) != .pipe) : (t += 1) if (a.tokenTag(t) == .identifier) {
        role[t] |= bit(.param);
    };
    if (t < a.tokens.len) pipes[t] = true;
}

/// folds.scm's nodes, as std.zig's node tags
const foldable = blk: {
    const Tag = Ast.Node.Tag;
    var t: [std.enums.values(Tag).len]bool = @splat(false);
    @setEvalBranchQuota(100000);
    for (std.enums.values(Tag)) |x| {
        const n = @tagName(x);
        for (.{ "block", "switch", "struct_init", "array_init", "asm", "multiline_string_literal", "if", "while", "for", "call", "container_decl", "tagged_union", "error_set_decl", "fn_proto" }) |p| if (std.mem.startsWith(u8, n, p)) {
            t[@intFromEnum(x)] = true;
        };
        for (.{ "switch_case", "switch_range", "for_range", "asm_output", "asm_input" }) |p| if (std.mem.startsWith(u8, n, p)) {
            t[@intFromEnum(x)] = false;
        };
    }
    break :blk t;
};

fn end(a: *const Ast, n: Ast.Node.Index) u32 {
    const l = a.lastToken(n);
    return @intCast(a.tokenStart(l) + a.tokenSlice(l).len);
}

fn before(_: void, x: [2]u32, y: [2]u32) bool {
    return x[0] < y[0] or (x[0] == y[0] and x[1] > y[1]);
}

/// folds.scm's captures as byte spans, in tree-sitter's order (a node before
/// the nodes inside it): syntax.c makes them levels
fn folds(h: *H, bad: []const bool) bool {
    const a = &h.ast;
    var sp: std.ArrayListUnmanaged([2]u32) = .empty;
    defer sp.deinit(gpa);
    var buf1: [1]Ast.Node.Index = undefined;
    var buf2: [2]Ast.Node.Index = undefined;
    // tree-sitter has an else_clause only in a statement, the else of an
    // else if too (else_clause alternative: (statement))
    const stmt = gpa.alloc(bool, a.nodes.len) catch return false;
    defer gpa.free(stmt);
    @memset(stmt, false);
    for (0..a.nodes.len) |i| if (!bad[a.nodeMainToken(@enumFromInt(i))]) if (a.blockStatements(&buf2, @enumFromInt(i))) |ss| for (ss) |x| {
        var y = x;
        while (true) {
            stmt[@intFromEnum(y)] = true;
            y = if (a.fullIf(y)) |f| f.ast.else_expr.unwrap() orelse break else break;
        }
    };
    for (1..a.nodes.len) |i| {
        const n: Ast.Node.Index = @enumFromInt(i);
        const tag = a.nodeTag(n);
        if (!foldable[@intFromEnum(tag)] or bad[a.nodeMainToken(n)]) continue;
        if (a.fullFnProto(&buf1, n)) |p| {
            // (parameters), from its ( to its )
            var r = p.lparen;
            var d: u32 = 0;
            while (true) : (r += 1) switch (a.tokenTag(r)) {
                .l_paren => d += 1,
                .r_paren => {
                    d -= 1;
                    if (d == 0) break;
                },
                else => {},
            };
            sp.append(gpa, .{ a.tokenStart(p.lparen), a.tokenStart(r) + 1 }) catch return false;
            continue;
        }
        // (initializer_list) starts at its {
        const init = std.mem.startsWith(u8, @tagName(tag), "struct_init") or std.mem.startsWith(u8, @tagName(tag), "array_init");
        sp.append(gpa, .{ a.tokenStart(if (init) a.nodeMainToken(n) else a.firstToken(n)), end(a, n) }) catch return false;
        if (!stmt[i]) continue;
        const e: ?[2]u32 = if (a.fullIf(n)) |x|
            (if (x.ast.else_expr.unwrap()) |e| .{ x.else_token, end(a, e) } else null)
        else if (a.fullWhile(n)) |x|
            (if (x.ast.else_expr.unwrap()) |e| .{ x.else_token, end(a, e) } else null)
        else if (a.fullFor(n)) |x|
            (if (x.ast.else_expr.unwrap()) |e| .{ x.else_token.?, end(a, e) } else null)
        else
            null;
        if (e) |x| sp.append(gpa, .{ a.tokenStart(x[0]), x[1] }) catch return false;
    }
    std.mem.sort([2]u32, sp.items, {}, before);
    h.folds.ensureTotalCapacity(gpa, 2 * sp.items.len) catch return false;
    for (sp.items) |x| h.folds.appendSliceAssumeCapacity(&x);
    return true;
}
