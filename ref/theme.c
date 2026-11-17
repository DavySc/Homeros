/* The colour theme every backend draws faces with (grid.h). */
#include "grid.h"

/* Catppuccin Mocha (FEATURES.md: Catppuccin, flavour toggle later): per
   face the colours and style that catppuccin/nvim's treesitter.lua gives the capture, with
   its default styles (comments, conditionals and "miscs" italic), links
   followed and unlisted captures falling back to their parent.  Its
   per-language overrides (@markup.heading.1.markdown and the like) have no
   face of their own. */
const Face faces[FACE_COUNT] = {
	{ {0, 0, 0}, {0, 0, 0}, 0 },                 /* none */
	{ {205, 214, 244}, {0, 0, 0}, FACE_FG },     /* variable: text */
	{ {243, 139, 168}, {0, 0, 0}, FACE_FG },     /* variable.builtin: red */
	{ {235, 160, 172}, {0, 0, 0}, FACE_FG },     /* variable.parameter: maroon */
	{ {235, 160, 172}, {0, 0, 0}, FACE_FG },     /* variable.parameter.builtin: maroon */
	{ {180, 190, 254}, {0, 0, 0}, FACE_FG },     /* variable.member: lavender */
	{ {250, 179, 135}, {0, 0, 0}, FACE_FG },     /* constant: peach */
	{ {250, 179, 135}, {0, 0, 0}, FACE_FG },     /* constant.builtin: peach */
	{ {203, 166, 247}, {0, 0, 0}, FACE_FG },     /* constant.macro: mauve */
	{ {249, 226, 175}, {0, 0, 0}, FACE_ITALIC | FACE_FG }, /* module: yellow, italic */
	{ {249, 226, 175}, {0, 0, 0}, FACE_ITALIC | FACE_FG }, /* module.builtin: yellow, italic */
	{ {116, 199, 236}, {0, 0, 0}, FACE_FG },     /* label: sapphire */
	{ {166, 227, 161}, {0, 0, 0}, FACE_FG },     /* string: green */
	{ {148, 226, 213}, {0, 0, 0}, FACE_FG },     /* string.documentation: teal */
	{ {245, 194, 231}, {0, 0, 0}, FACE_FG },     /* string.regexp: pink */
	{ {245, 194, 231}, {0, 0, 0}, FACE_FG },     /* string.escape: pink */
	{ {245, 194, 231}, {0, 0, 0}, FACE_FG },     /* string.special: pink */
	{ {242, 205, 205}, {0, 0, 0}, FACE_FG },     /* string.special.symbol: flamingo */
	{ {245, 194, 231}, {0, 0, 0}, FACE_FG },     /* string.special.path: pink */
	{ {137, 180, 250}, {0, 0, 0}, FACE_ITALIC | FACE_UNDERLINE | FACE_FG }, /* string.special.url: blue, italic underline */
	{ {148, 226, 213}, {0, 0, 0}, FACE_FG },     /* character: teal */
	{ {245, 194, 231}, {0, 0, 0}, FACE_FG },     /* character.special: pink */
	{ {250, 179, 135}, {0, 0, 0}, FACE_FG },     /* boolean: peach */
	{ {250, 179, 135}, {0, 0, 0}, FACE_FG },     /* number: peach */
	{ {250, 179, 135}, {0, 0, 0}, FACE_FG },     /* number.float: peach */
	{ {249, 226, 175}, {0, 0, 0}, FACE_FG },     /* type: yellow */
	{ {203, 166, 247}, {0, 0, 0}, FACE_FG },     /* type.builtin: mauve */
	{ {249, 226, 175}, {0, 0, 0}, FACE_FG },     /* type.definition: yellow */
	{ {250, 179, 135}, {0, 0, 0}, FACE_FG },     /* attribute: peach */
	{ {250, 179, 135}, {0, 0, 0}, FACE_FG },     /* attribute.builtin: peach */
	{ {180, 190, 254}, {0, 0, 0}, FACE_FG },     /* property: lavender */
	{ {137, 180, 250}, {0, 0, 0}, FACE_FG },     /* function: blue */
	{ {250, 179, 135}, {0, 0, 0}, FACE_FG },     /* function.builtin: peach */
	{ {137, 180, 250}, {0, 0, 0}, FACE_FG },     /* function.call: blue */
	{ {245, 194, 231}, {0, 0, 0}, FACE_FG },     /* function.macro: pink */
	{ {137, 180, 250}, {0, 0, 0}, FACE_FG },     /* function.method: blue */
	{ {137, 180, 250}, {0, 0, 0}, FACE_FG },     /* function.method.call: blue */
	{ {249, 226, 175}, {0, 0, 0}, FACE_FG },     /* constructor: yellow */
	{ {137, 220, 235}, {0, 0, 0}, FACE_FG },     /* operator: sky */
	{ {203, 166, 247}, {0, 0, 0}, FACE_FG },     /* keyword: mauve */
	{ {203, 166, 247}, {0, 0, 0}, FACE_FG },     /* keyword.coroutine: mauve */
	{ {203, 166, 247}, {0, 0, 0}, FACE_FG },     /* keyword.function: mauve */
	{ {203, 166, 247}, {0, 0, 0}, FACE_FG },     /* keyword.operator: mauve */
	{ {203, 166, 247}, {0, 0, 0}, FACE_FG },     /* keyword.import: mauve */
	{ {203, 166, 247}, {0, 0, 0}, FACE_FG },     /* keyword.type: mauve */
	{ {203, 166, 247}, {0, 0, 0}, FACE_FG },     /* keyword.modifier: mauve */
	{ {203, 166, 247}, {0, 0, 0}, FACE_FG },     /* keyword.repeat: mauve */
	{ {203, 166, 247}, {0, 0, 0}, FACE_FG },     /* keyword.return: mauve */
	{ {203, 166, 247}, {0, 0, 0}, FACE_FG },     /* keyword.debug: mauve */
	{ {203, 166, 247}, {0, 0, 0}, FACE_FG },     /* keyword.exception: mauve */
	{ {203, 166, 247}, {0, 0, 0}, FACE_ITALIC | FACE_FG }, /* keyword.conditional: mauve, italic */
	{ {137, 220, 235}, {0, 0, 0}, FACE_FG },     /* keyword.conditional.ternary: sky */
	{ {245, 194, 231}, {0, 0, 0}, FACE_FG },     /* keyword.directive: pink */
	{ {245, 194, 231}, {0, 0, 0}, FACE_FG },     /* keyword.directive.define: pink */
	{ {147, 153, 178}, {0, 0, 0}, FACE_FG },     /* punctuation.delimiter: overlay2 */
	{ {147, 153, 178}, {0, 0, 0}, FACE_FG },     /* punctuation.bracket: overlay2 */
	{ {245, 194, 231}, {0, 0, 0}, FACE_FG },     /* punctuation.special: pink */
	{ {147, 153, 178}, {0, 0, 0}, FACE_ITALIC | FACE_FG }, /* comment: overlay2, italic */
	{ {147, 153, 178}, {0, 0, 0}, FACE_ITALIC | FACE_FG }, /* comment.documentation: overlay2, italic */
	{ {30, 30, 46}, {243, 139, 168}, FACE_FG | FACE_BG }, /* comment.error: base on red */
	{ {30, 30, 46}, {249, 226, 175}, FACE_FG | FACE_BG }, /* comment.warning: base on yellow */
	{ {30, 30, 46}, {242, 205, 205}, FACE_FG | FACE_BG }, /* comment.todo: base on flamingo */
	{ {30, 30, 46}, {245, 224, 220}, FACE_FG | FACE_BG }, /* comment.note: base on rosewater */
	{ {243, 139, 168}, {0, 0, 0}, FACE_BOLD | FACE_FG }, /* markup.strong: red, bold */
	{ {243, 139, 168}, {0, 0, 0}, FACE_ITALIC | FACE_FG }, /* markup.italic: red, italic */
	{ {205, 214, 244}, {0, 0, 0}, FACE_STRIKE | FACE_FG }, /* markup.strikethrough: text, strikethrough */
	{ {0, 0, 0}, {0, 0, 0}, FACE_UNDERLINE },    /* markup.underline: underline */
	{ {137, 180, 250}, {0, 0, 0}, FACE_FG },     /* markup.heading: blue */
	{ {137, 180, 250}, {0, 0, 0}, FACE_FG },     /* markup.heading.1: blue */
	{ {137, 180, 250}, {0, 0, 0}, FACE_FG },     /* markup.heading.2: blue */
	{ {137, 180, 250}, {0, 0, 0}, FACE_FG },     /* markup.heading.3: blue */
	{ {137, 180, 250}, {0, 0, 0}, FACE_FG },     /* markup.heading.4: blue */
	{ {137, 180, 250}, {0, 0, 0}, FACE_FG },     /* markup.heading.5: blue */
	{ {137, 180, 250}, {0, 0, 0}, FACE_FG },     /* markup.heading.6: blue */
	{ {245, 194, 231}, {0, 0, 0}, FACE_FG },     /* markup.quote: pink */
	{ {137, 180, 250}, {0, 0, 0}, FACE_FG },     /* markup.math: blue */
	{ {180, 190, 254}, {0, 0, 0}, FACE_FG },     /* markup.link: lavender */
	{ {180, 190, 254}, {0, 0, 0}, FACE_FG },     /* markup.link.label: lavender */
	{ {137, 180, 250}, {0, 0, 0}, FACE_ITALIC | FACE_UNDERLINE | FACE_FG }, /* markup.link.url: blue, italic underline */
	{ {166, 227, 161}, {0, 0, 0}, FACE_FG },     /* markup.raw: green */
	{ {166, 227, 161}, {0, 0, 0}, FACE_FG },     /* markup.raw.block: green */
	{ {148, 226, 213}, {0, 0, 0}, FACE_FG },     /* markup.list: teal */
	{ {166, 227, 161}, {0, 0, 0}, FACE_FG },     /* markup.list.checked: green */
	{ {127, 132, 156}, {0, 0, 0}, FACE_FG },     /* markup.list.unchecked: overlay1 */
	{ {166, 227, 161}, {0, 0, 0}, FACE_FG },     /* diff.plus: green */
	{ {243, 139, 168}, {0, 0, 0}, FACE_FG },     /* diff.minus: red */
	{ {137, 180, 250}, {0, 0, 0}, FACE_FG },     /* diff.delta: blue */
	{ {137, 180, 250}, {0, 0, 0}, FACE_FG },     /* tag: blue */
	{ {137, 180, 250}, {0, 0, 0}, FACE_FG },     /* tag.builtin: blue */
	{ {249, 226, 175}, {0, 0, 0}, FACE_ITALIC | FACE_FG }, /* tag.attribute: yellow, italic */
	{ {148, 226, 213}, {0, 0, 0}, FACE_FG },     /* tag.delimiter: teal */
};
