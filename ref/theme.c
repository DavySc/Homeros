/* The colour theme every backend draws faces with (grid.h). */
#include "grid.h"

/* Catppuccin Mocha (FEATURES.md: Catppuccin, flavour toggle later): per
   face the colours and style that catppuccin/nvim's treesitter.lua gives the capture, with
   its default styles (comments, conditionals and "miscs" italic), links
   followed and unlisted captures falling back to their parent, or to
   Neovim's default link (Special) where catppuccin sets none.  Of its
   per-language overrides only @markup.heading.1.markdown to 6 are taken
   (rainbow1 to 6), for markup.heading.1 to 6: no other grammar here has
   those captures. */
const Face faces[FACE_COUNT] = {
	{ {0, 0, 0}, {0, 0, 0}, 0, {0} },                 /* none */
	{ {205, 214, 244}, {0, 0, 0}, FACE_FG, {0} },     /* variable: text */
	{ {243, 139, 168}, {0, 0, 0}, FACE_FG, {0} },     /* variable.builtin: red */
	{ {235, 160, 172}, {0, 0, 0}, FACE_FG, {0} },     /* variable.parameter: maroon */
	{ {245, 194, 231}, {0, 0, 0}, FACE_FG, {0} },     /* variable.parameter.builtin: pink (Neovim's link to Special) */
	{ {180, 190, 254}, {0, 0, 0}, FACE_FG, {0} },     /* variable.member: lavender */
	{ {250, 179, 135}, {0, 0, 0}, FACE_FG, {0} },     /* constant: peach */
	{ {250, 179, 135}, {0, 0, 0}, FACE_FG, {0} },     /* constant.builtin: peach */
	{ {203, 166, 247}, {0, 0, 0}, FACE_FG, {0} },     /* constant.macro: mauve */
	{ {249, 226, 175}, {0, 0, 0}, FACE_ITALIC | FACE_FG, {0} }, /* module: yellow, italic */
	{ {245, 194, 231}, {0, 0, 0}, FACE_FG, {0} },     /* module.builtin: pink (Neovim's link to Special) */
	{ {116, 199, 236}, {0, 0, 0}, FACE_FG, {0} },     /* label: sapphire */
	{ {166, 227, 161}, {0, 0, 0}, FACE_FG, {0} },     /* string: green */
	{ {148, 226, 213}, {0, 0, 0}, FACE_FG, {0} },     /* string.documentation: teal */
	{ {245, 194, 231}, {0, 0, 0}, FACE_FG, {0} },     /* string.regexp: pink */
	{ {245, 194, 231}, {0, 0, 0}, FACE_FG, {0} },     /* string.escape: pink */
	{ {245, 194, 231}, {0, 0, 0}, FACE_FG, {0} },     /* string.special: pink */
	{ {242, 205, 205}, {0, 0, 0}, FACE_FG, {0} },     /* string.special.symbol: flamingo */
	{ {245, 194, 231}, {0, 0, 0}, FACE_FG, {0} },     /* string.special.path: pink */
	{ {137, 180, 250}, {0, 0, 0}, FACE_ITALIC | FACE_UNDERLINE | FACE_FG, {0} }, /* string.special.url: blue, italic underline */
	{ {148, 226, 213}, {0, 0, 0}, FACE_FG, {0} },     /* character: teal */
	{ {245, 194, 231}, {0, 0, 0}, FACE_FG, {0} },     /* character.special: pink */
	{ {250, 179, 135}, {0, 0, 0}, FACE_FG, {0} },     /* boolean: peach */
	{ {250, 179, 135}, {0, 0, 0}, FACE_FG, {0} },     /* number: peach */
	{ {250, 179, 135}, {0, 0, 0}, FACE_FG, {0} },     /* number.float: peach */
	{ {249, 226, 175}, {0, 0, 0}, FACE_FG, {0} },     /* type: yellow */
	{ {203, 166, 247}, {0, 0, 0}, FACE_FG, {0} },     /* type.builtin: mauve */
	{ {249, 226, 175}, {0, 0, 0}, FACE_FG, {0} },     /* type.definition: yellow */
	{ {250, 179, 135}, {0, 0, 0}, FACE_FG, {0} },     /* attribute: peach */
	{ {245, 194, 231}, {0, 0, 0}, FACE_FG, {0} },     /* attribute.builtin: pink (Neovim's link to Special) */
	{ {180, 190, 254}, {0, 0, 0}, FACE_FG, {0} },     /* property: lavender */
	{ {137, 180, 250}, {0, 0, 0}, FACE_FG, {0} },     /* function: blue */
	{ {250, 179, 135}, {0, 0, 0}, FACE_FG, {0} },     /* function.builtin: peach */
	{ {137, 180, 250}, {0, 0, 0}, FACE_FG, {0} },     /* function.call: blue */
	{ {245, 194, 231}, {0, 0, 0}, FACE_FG, {0} },     /* function.macro: pink */
	{ {137, 180, 250}, {0, 0, 0}, FACE_FG, {0} },     /* function.method: blue */
	{ {137, 180, 250}, {0, 0, 0}, FACE_FG, {0} },     /* function.method.call: blue */
	{ {249, 226, 175}, {0, 0, 0}, FACE_FG, {0} },     /* constructor: yellow */
	{ {137, 220, 235}, {0, 0, 0}, FACE_FG, {0} },     /* operator: sky */
	{ {203, 166, 247}, {0, 0, 0}, FACE_FG, {0} },     /* keyword: mauve */
	{ {203, 166, 247}, {0, 0, 0}, FACE_FG, {0} },     /* keyword.coroutine: mauve */
	{ {203, 166, 247}, {0, 0, 0}, FACE_FG, {0} },     /* keyword.function: mauve */
	{ {203, 166, 247}, {0, 0, 0}, FACE_FG, {0} },     /* keyword.operator: mauve */
	{ {203, 166, 247}, {0, 0, 0}, FACE_FG, {0} },     /* keyword.import: mauve */
	{ {203, 166, 247}, {0, 0, 0}, FACE_FG, {0} },     /* keyword.type: mauve */
	{ {203, 166, 247}, {0, 0, 0}, FACE_FG, {0} },     /* keyword.modifier: mauve */
	{ {203, 166, 247}, {0, 0, 0}, FACE_FG, {0} },     /* keyword.repeat: mauve */
	{ {203, 166, 247}, {0, 0, 0}, FACE_FG, {0} },     /* keyword.return: mauve */
	{ {203, 166, 247}, {0, 0, 0}, FACE_FG, {0} },     /* keyword.debug: mauve */
	{ {203, 166, 247}, {0, 0, 0}, FACE_FG, {0} },     /* keyword.exception: mauve */
	{ {203, 166, 247}, {0, 0, 0}, FACE_ITALIC | FACE_FG, {0} }, /* keyword.conditional: mauve, italic */
	{ {137, 220, 235}, {0, 0, 0}, FACE_FG, {0} },     /* keyword.conditional.ternary: sky */
	{ {245, 194, 231}, {0, 0, 0}, FACE_FG, {0} },     /* keyword.directive: pink */
	{ {245, 194, 231}, {0, 0, 0}, FACE_FG, {0} },     /* keyword.directive.define: pink */
	{ {147, 153, 178}, {0, 0, 0}, FACE_FG, {0} },     /* punctuation.delimiter: overlay2 */
	{ {147, 153, 178}, {0, 0, 0}, FACE_FG, {0} },     /* punctuation.bracket: overlay2 */
	{ {245, 194, 231}, {0, 0, 0}, FACE_FG, {0} },     /* punctuation.special: pink */
	{ {147, 153, 178}, {0, 0, 0}, FACE_ITALIC | FACE_FG, {0} }, /* comment: overlay2, italic */
	{ {147, 153, 178}, {0, 0, 0}, FACE_ITALIC | FACE_FG, {0} }, /* comment.documentation: overlay2, italic */
	{ {30, 30, 46}, {243, 139, 168}, FACE_FG | FACE_BG, {0} }, /* comment.error: base on red */
	{ {30, 30, 46}, {249, 226, 175}, FACE_FG | FACE_BG, {0} }, /* comment.warning: base on yellow */
	{ {30, 30, 46}, {242, 205, 205}, FACE_FG | FACE_BG, {0} }, /* comment.todo: base on flamingo */
	{ {30, 30, 46}, {137, 180, 250}, FACE_FG | FACE_BG, {0} }, /* comment.note: base on blue (catppuccin sets it to @comment.hint) */
	{ {243, 139, 168}, {0, 0, 0}, FACE_BOLD | FACE_FG, {0} }, /* markup.strong: red, bold */
	{ {243, 139, 168}, {0, 0, 0}, FACE_ITALIC | FACE_FG, {0} }, /* markup.italic: red, italic */
	{ {205, 214, 244}, {0, 0, 0}, FACE_STRIKE | FACE_FG, {0} }, /* markup.strikethrough: text, strikethrough */
	{ {0, 0, 0}, {0, 0, 0}, FACE_UNDERLINE, {0} },    /* markup.underline: underline */
	{ {137, 180, 250}, {0, 0, 0}, FACE_FG, {0} },     /* markup.heading: blue */
	{ {243, 139, 168}, {0, 0, 0}, FACE_FG, {0} },     /* markup.heading.1: red (rainbow1) */
	{ {250, 179, 135}, {0, 0, 0}, FACE_FG, {0} },     /* markup.heading.2: peach (rainbow2) */
	{ {249, 226, 175}, {0, 0, 0}, FACE_FG, {0} },     /* markup.heading.3: yellow (rainbow3) */
	{ {166, 227, 161}, {0, 0, 0}, FACE_FG, {0} },     /* markup.heading.4: green (rainbow4) */
	{ {116, 199, 236}, {0, 0, 0}, FACE_FG, {0} },     /* markup.heading.5: sapphire (rainbow5) */
	{ {180, 190, 254}, {0, 0, 0}, FACE_FG, {0} },     /* markup.heading.6: lavender (rainbow6) */
	{ {245, 194, 231}, {0, 0, 0}, FACE_FG, {0} },     /* markup.quote: pink */
	{ {137, 180, 250}, {0, 0, 0}, FACE_FG, {0} },     /* markup.math: blue */
	{ {180, 190, 254}, {0, 0, 0}, FACE_FG, {0} },     /* markup.link: lavender */
	{ {180, 190, 254}, {0, 0, 0}, FACE_FG, {0} },     /* markup.link.label: lavender */
	{ {137, 180, 250}, {0, 0, 0}, FACE_ITALIC | FACE_UNDERLINE | FACE_FG, {0} }, /* markup.link.url: blue, italic underline */
	{ {166, 227, 161}, {0, 0, 0}, FACE_FG, {0} },     /* markup.raw: green */
	{ {166, 227, 161}, {0, 0, 0}, FACE_FG, {0} },     /* markup.raw.block: green */
	{ {148, 226, 213}, {0, 0, 0}, FACE_FG, {0} },     /* markup.list: teal */
	{ {166, 227, 161}, {0, 0, 0}, FACE_FG, {0} },     /* markup.list.checked: green */
	{ {127, 132, 156}, {0, 0, 0}, FACE_FG, {0} },     /* markup.list.unchecked: overlay1 */
	{ {166, 227, 161}, {0, 0, 0}, FACE_FG, {0} },     /* diff.plus: green */
	{ {243, 139, 168}, {0, 0, 0}, FACE_FG, {0} },     /* diff.minus: red */
	{ {137, 180, 250}, {0, 0, 0}, FACE_FG, {0} },     /* diff.delta: blue */
	{ {137, 180, 250}, {0, 0, 0}, FACE_FG, {0} },     /* tag: blue */
	{ {137, 180, 250}, {0, 0, 0}, FACE_FG, {0} },     /* tag.builtin: blue */
	{ {249, 226, 175}, {0, 0, 0}, FACE_ITALIC | FACE_FG, {0} }, /* tag.attribute: yellow, italic */
	{ {148, 226, 213}, {0, 0, 0}, FACE_FG, {0} },     /* tag.delimiter: teal */
	/* Org's faces as catppuccin/emacs (56c3cbe) gives them in Mocha; its
	   org-drawer is Org's own LightSkyBlue, here sky, and its block lines
	   take our comment's look on the block's mantle */
	{ {203, 166, 247}, {0, 0, 0}, FACE_FG, {0} },     /* org-level-7: mauve */
	{ {235, 160, 172}, {0, 0, 0}, FACE_FG, {0} },     /* org-level-8: maroon */
	{ {250, 179, 135}, {0, 0, 0}, FACE_FG, {0} },     /* org-todo: peach */
	{ {249, 226, 175}, {0, 0, 0}, FACE_FG, {0} },     /* org-priority: yellow */
	{ {203, 166, 247}, {0, 0, 0}, FACE_BOLD | FACE_FG, {0} }, /* org-tag: mauve, bold */
	{ {166, 173, 200}, {0, 0, 0}, FACE_FG, {0} },     /* org-date: subtext0 */
	{ {108, 112, 134}, {0, 0, 0}, FACE_FG, {0} },     /* org-table: overlay0 */
	{ {245, 194, 231}, {0, 0, 0}, FACE_FG, {0} },     /* org-formula: pink */
	{ {137, 220, 235}, {0, 0, 0}, FACE_FG, {0} },     /* org-drawer: sky */
	{ {0, 0, 0}, {0, 0, 0}, FACE_BOLD, {0} },         /* org-checkbox: bold */
	{ {116, 199, 236}, {0, 0, 0}, FACE_FG, {0} },     /* org-document-info: sapphire */
	{ {166, 227, 161}, {24, 24, 37}, FACE_FG | FACE_BG, {0} }, /* org-block: green on mantle */
	{ {147, 153, 178}, {24, 24, 37}, FACE_ITALIC | FACE_FG | FACE_BG, {0} }, /* org-block-begin-line: overlay2, italic, on mantle */
	/* catppuccin/nvim's render_markdown integration: rainbow N darkened
	   into base by 0.095, its value without transparent_background (Davy
	   chose the opaque Mocha base; measured in nvim 0.12.6, catppuccin
	   05e8787, reviews/pr-57.md) */
	{ {0, 0, 0}, {50, 40, 58}, FACE_BG, {0} },     /* heading 1 background: red 9.5% on base */
	{ {0, 0, 0}, {51, 44, 54}, FACE_BG, {0} },     /* heading 2 background: peach 9.5% on base */
	{ {0, 0, 0}, {51, 49, 58}, FACE_BG, {0} },     /* heading 3 background: yellow 9.5% on base */
	{ {0, 0, 0}, {43, 49, 57}, FACE_BG, {0} },     /* heading 4 background: green 9.5% on base */
	{ {0, 0, 0}, {38, 46, 64}, FACE_BG, {0} },     /* heading 5 background: sapphire 9.5% on base */
	{ {0, 0, 0}, {44, 45, 66}, FACE_BG, {0} },     /* heading 6 background: lavender 9.5% on base */
	/* the UI groups (Neovim, lualine, gitsigns, snacks, which-key)
	   as a live Neovim 0.12.6 with catppuccin 05e8787 (my-nvim's pin) shows
	   them, opaque (measured: project files ui-look/hl-opaque.txt).  The
	   diagnostic span is an undercurl, Davy's pick, where catppuccin underlines */
	{ {0, 0, 0}, {42, 43, 60}, FACE_BG, {0} },  /* cursor line: CursorLine */
	{ {180, 190, 254}, {0, 0, 0}, FACE_FG, {0} },  /* cursor line nr: CursorLineNr */
	{ {69, 71, 90}, {0, 0, 0}, FACE_FG, {0} },  /* line nr: LineNr */
	{ {205, 214, 244}, {24, 24, 37}, FACE_FG | FACE_BG, {0} },  /* status line: StatusLine, lualine_c */
	{ {108, 112, 134}, {24, 24, 37}, FACE_FG | FACE_BG, {0} },  /* status line nc: lualine_c_inactive */
	{ {17, 17, 27}, {0, 0, 0}, FACE_FG, {0} },  /* win separator: WinSeparator */
	{ {69, 71, 90}, {0, 0, 0}, FACE_FG, {0} },  /* end of buffer: EndOfBuffer */
	{ {108, 112, 134}, {0, 0, 0}, FACE_FG, {0} },  /* non text: NonText, overlay0 (catppuccin editor.lua:34) */
	{ {0, 0, 0}, {69, 71, 90}, FACE_BOLD | FACE_BG, {0} },  /* visual: Visual */
	{ {205, 214, 244}, {62, 87, 103}, FACE_FG | FACE_BG, {0} },  /* search: Search */
	{ {24, 24, 37}, {243, 139, 168}, FACE_FG | FACE_BG, {0} },  /* cur search: CurSearch */
	{ {250, 179, 135}, {57, 59, 77}, FACE_BOLD | FACE_FG | FACE_BG, {0} },  /* match paren: MatchParen */
	{ {147, 153, 178}, {24, 24, 37}, FACE_FG | FACE_BG, {0} },  /* pmenu: Pmenu */
	{ {0, 0, 0}, {49, 50, 68}, FACE_BOLD | FACE_BG, {0} },  /* pmenu sel: PmenuSel */
	{ {0, 0, 0}, {49, 50, 68}, FACE_BG, {0} },  /* pmenu sbar: PmenuSbar, surface0 (editor.lua:72) */
	{ {0, 0, 0}, {108, 112, 134}, FACE_BG, {0} },  /* pmenu thumb: PmenuThumb, overlay0 (editor.lua:73) */
	{ {205, 214, 244}, {24, 24, 37}, FACE_FG | FACE_BG, {0} },  /* normal float: NormalFloat, WhichKey */
	{ {137, 180, 250}, {24, 24, 37}, FACE_FG | FACE_BG, {0} },  /* float border: FloatBorder, SnacksPickerBorder, WhichKeyBorder */
	{ {166, 173, 200}, {24, 24, 37}, FACE_FG | FACE_BG, {0} },  /* float title: FloatTitle */
	{ {30, 30, 46}, {245, 224, 220}, FACE_FG | FACE_BG, {0} },  /* cursor: Cursor */
	{ {24, 24, 37}, {137, 180, 250}, FACE_BOLD | FACE_FG | FACE_BG, {0} },  /* mode normal: lualine_a_normal */
	{ {30, 30, 46}, {166, 227, 161}, FACE_BOLD | FACE_FG | FACE_BG, {0} },  /* mode insert: lualine_a_insert */
	{ {30, 30, 46}, {203, 166, 247}, FACE_BOLD | FACE_FG | FACE_BG, {0} },  /* mode visual: lualine_a_visual */
	{ {30, 30, 46}, {243, 139, 168}, FACE_BOLD | FACE_FG | FACE_BG, {0} },  /* mode replace: lualine_a_replace */
	{ {30, 30, 46}, {250, 179, 135}, FACE_BOLD | FACE_FG | FACE_BG, {0} },  /* mode command: lualine_a_command */
	{ {30, 30, 46}, {148, 226, 213}, FACE_BOLD | FACE_FG | FACE_BG, {0} },  /* mode other: lualine_a_terminal */
	{ {137, 180, 250}, {24, 24, 37}, FACE_FG | FACE_BG, {0} },  /* mode normal fg: lualine_b_normal's fg on mantle */
	{ {166, 227, 161}, {24, 24, 37}, FACE_FG | FACE_BG, {0} },  /* mode insert fg */
	{ {203, 166, 247}, {24, 24, 37}, FACE_FG | FACE_BG, {0} },  /* mode visual fg */
	{ {243, 139, 168}, {24, 24, 37}, FACE_FG | FACE_BG, {0} },  /* mode replace fg */
	{ {250, 179, 135}, {24, 24, 37}, FACE_FG | FACE_BG, {0} },  /* mode command fg */
	{ {49, 50, 68}, {49, 50, 68}, FACE_FG | FACE_BG, {0} },  /* status info: lualine_b's bg, the fg of its rounded end */
	{ {166, 227, 161}, {0, 0, 0}, FACE_FG, {0} },  /* git add: GitSignsAdd */
	{ {249, 226, 175}, {0, 0, 0}, FACE_FG, {0} },  /* git change: GitSignsChange */
	{ {243, 139, 168}, {0, 0, 0}, FACE_FG, {0} },  /* git delete: GitSignsDelete */
	{ {243, 139, 168}, {0, 0, 0}, FACE_ITALIC | FACE_FG, {0} },  /* diag error: DiagnosticError */
	{ {249, 226, 175}, {0, 0, 0}, FACE_ITALIC | FACE_FG, {0} },  /* diag warn: DiagnosticWarn */
	{ {137, 220, 235}, {0, 0, 0}, FACE_ITALIC | FACE_FG, {0} },  /* diag info: DiagnosticInfo */
	{ {148, 226, 213}, {0, 0, 0}, FACE_ITALIC | FACE_FG, {0} },  /* diag hint: DiagnosticHint */
	{ {0, 0, 0}, {0, 0, 0}, FACE_UNDERCURL | FACE_SP, {243, 139, 168} },  /* diag under error: DiagnosticUnderlineError */
	{ {0, 0, 0}, {0, 0, 0}, FACE_UNDERCURL | FACE_SP, {249, 226, 175} },  /* diag under warn: DiagnosticUnderlineWarn */
	{ {0, 0, 0}, {0, 0, 0}, FACE_UNDERCURL | FACE_SP, {137, 220, 235} },  /* diag under info: DiagnosticUnderlineInfo */
	{ {0, 0, 0}, {0, 0, 0}, FACE_UNDERCURL | FACE_SP, {148, 226, 213} },  /* diag under hint: DiagnosticUnderlineHint */
	{ {243, 139, 168}, {50, 40, 58}, FACE_ITALIC | FACE_FG | FACE_BG, {0} },  /* diag vt error: DiagnosticVirtualTextError */
	{ {249, 226, 175}, {51, 49, 58}, FACE_ITALIC | FACE_FG | FACE_BG, {0} },  /* diag vt warn: DiagnosticVirtualTextWarn */
	{ {137, 220, 235}, {40, 48, 64}, FACE_ITALIC | FACE_FG | FACE_BG, {0} },  /* diag vt info: DiagnosticVirtualTextInfo */
	{ {148, 226, 213}, {41, 49, 62}, FACE_ITALIC | FACE_FG | FACE_BG, {0} },  /* diag vt hint: DiagnosticVirtualTextHint */
	{ {49, 50, 68}, {0, 0, 0}, FACE_FG, {0} },  /* indent: SnacksIndent */
	{ {147, 153, 178}, {0, 0, 0}, FACE_FG, {0} },  /* indent scope: SnacksIndentScope */
	{ {137, 180, 250}, {0, 0, 0}, FACE_FG, {0} },  /* pick match: SnacksPickerMatch */
	{ {242, 205, 205}, {0, 0, 0}, FACE_FG, {0} },  /* pick prompt: SnacksPickerPrompt */
	{ {137, 180, 250}, {0, 0, 0}, FACE_FG, {0} },  /* which-key group: WhichKeyGroup */
	{ {245, 194, 231}, {0, 0, 0}, FACE_FG, {0} },  /* which-key desc: WhichKeyDesc */
	{ {108, 112, 134}, {0, 0, 0}, FACE_FG, {0} },  /* which-key separator: WhichKeySeparator */
	/* the save kitten: kitten.lua's colours, bold but the dim one */
	{ {203, 166, 247}, {0, 0, 0}, FACE_BOLD | FACE_FG, {0} },  /* kitten cat: mauve */
	{ {166, 227, 161}, {0, 0, 0}, FACE_BOLD | FACE_FG, {0} },  /* kitten face: green */
	{ {250, 179, 135}, {0, 0, 0}, FACE_BOLD | FACE_FG, {0} },  /* kitten paw: peach */
	{ {243, 139, 168}, {0, 0, 0}, FACE_BOLD | FACE_FG, {0} },  /* kitten heart: red */
	{ {245, 194, 231}, {0, 0, 0}, FACE_BOLD | FACE_FG, {0} },  /* kitten text: pink */
	{ {108, 112, 134}, {0, 0, 0}, FACE_FG, {0} },  /* kitten dim: overlay0 */
	/* the start screen: bold, in the app icon's colours (by design, not
	   catppuccin's SnacksDashboard groups) */
	{ {180, 190, 254}, {0, 0, 0}, FACE_BOLD | FACE_FG, {0} },  /* dashboard title: lavender */
	{ {137, 180, 250}, {0, 0, 0}, FACE_BOLD | FACE_FG, {0} },  /* dashboard footer: blue */
	{ {245, 224, 220}, {0, 0, 0}, FACE_BOLD | FACE_FG, {0} },  /* dashboard cat: rosewater */
	{ {127, 132, 156}, {0, 0, 0}, FACE_BOLD | FACE_FG, {0} },  /* dashboard cat patch: overlay1 */
	{ {166, 227, 161}, {0, 0, 0}, FACE_BOLD | FACE_FG, {0} },  /* dashboard frog: green */
	{ {235, 160, 172}, {0, 0, 0}, FACE_BOLD | FACE_FG, {0} },  /* dashboard frog cheek: maroon */
	{ {250, 179, 135}, {0, 0, 0}, FACE_BOLD | FACE_FG, {0} },  /* dashboard frog mouth: peach */
	{ {249, 226, 175}, {0, 0, 0}, FACE_BOLD | FACE_FG, {0} },  /* dashboard sparkle: yellow */
	{ {108, 112, 134}, {0, 0, 0}, FACE_BOLD | FACE_FG, {0} },  /* dashboard terminal: overlay0 */
	{ {203, 166, 247}, {0, 0, 0}, FACE_BOLD | FACE_FG, {0} },  /* dashboard prompt: mauve */
};
