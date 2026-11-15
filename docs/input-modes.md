# Input Modes

Normally, a terminal emulator only knows about one input mode, so
there is no need of distinction.

Inspired by Vi/Vim, the Termite terminal emulator started to introduce
so called Vim-modes, where the user can use the keyboard only to
screen text selection, amongst other things. So users can press
Ctrl+Shift+Space to enter the vim mode. You can then move the cursor
using vim motion keys and then start selecting.

### Contour introduces multiple Vim-like input modes:

- **insert mode**: This is the default input mode. Everything is forwarded to the application.
- **normal mode**: Use motion keys to move the cursor and operators to act
- **visual mode**: Linear selection, use motion keys to alter the selection.
- **visual line mode**: Line based selection, use motion keys to alter the selection.
- **visual block mode**: Block based selection, use motion keys to alter the selection.
- **[hint mode](demo/hint-mode.md)**: Overlay alphabetic labels on detected patterns (URLs, file paths, etc.) and type a label to act on it.

### Supported operators (Normal Mode)

- Normal: `[count] p` (paste primary clipboard `count` times)
- Normal: `yy` (yank current line to primary clipboard)
- Normal: `y {motion}` (yank given `motion` to primary clipboard)
- Normal: `y {TextObject}` (yank given `textObject` to primary clipboard, such as `yiw`, `yaw`, `yip`, `yap`, ...)
- Visual: `y` (yank current selection into primary clipboard)
- `v` enables/disables visual mode
- `V` enables/disables visual line mode
- `i` activates insert mode
- `Ctrl+v` enables/disables visual block mode
- Normal: `o {motion}` (opens resource by given `motion`, this can be a URL or local file)
- Normal: `o {TextObject}` (opens resource by given `textObject`, such as `oiw`, `oaw`, `oiW`, ...)
- Visual: `o` (open current selection into primary clipboard)
- Normal: `gh` (enter [hint mode](demo/hint-mode.md) — copy selected match to clipboard)
- Normal: `gH` (enter [hint mode](demo/hint-mode.md) — open selected match)
- Normal: `gx` (opens the link under the cursor: a URL, a path, or a custom pattern)
- Normal: `mm` (set or clear a [user mark](#bookmarking-a-line-in-vi-normal-mode) on the cursor line)
- Normal: `za` / `zo` / `zc` (toggle / open / close the [command block](demo/command-blocks.md#folding) under the cursor)
- Normal: `zM` / `zR` (close / open every command block)

### Supported motions

Moving the cursor outside of the current view using a motion, will cause
the terminal to scroll the view to make that target line visible.

- `[count] h`
- `[count] j`
- `[count] k`
- `[count] l`
- `[count] w`
- `[count] b`
- `[count] e`
- `[count] |`
- `0`
- `$`
- `gg`
- `G`
- `{` & `}`
- `[m` & `]m` (previous / next mark: a prompt or a user mark)
- `[count] ]x` (next link)
- `[count] [x` (previous link)

### Supported text objects

- `i<`, `a<` - angle brackets enclosed text
- `i{`, `a{` - curly brackets enclosed text
- `i"`, `a"` - double quotes enclosed text
- `ip`, `ap` - backtick enclosed string
- `i(`, `a(` - round brackets enclosed string
- `i'`, `a'` - single quoted string
- `` i` ``, `` a` `` - backtick enclosed string
- `i[`, `a[` - square bracket enclosed string
- `iw`, `aw` - regular word
- `iW`, `aW` - space delimited word
- `im`, `am` - the lines between two marks, without / with the marked lines

### Opening local files and URLs

Contour finds URLs, file paths and patterns of your own in the output and underlines them
(see [Links in Terminal Output](configuration/links.md)). In normal mode, `]x` and `[x` move the
cursor from one link to the next, scanning beyond the screen when needed, and `gx` opens the link
under the cursor as Ctrl+click would. A path counts once Contour has found that it exists.

You can also open what is not detected as a link by moving the cursor onto a resource (URL or local
file) and then pressing `oiW` (open in (space separated) word) or use any of the other supported text
objects to denote what to open.

You may as well type `oo` to open the URL under the current line. This works if no other information
is on this current line, while spaces are trimmed left and right of the given URL in this line.

## Select and yank

![type:video](videos/contour-normal-mode-select-and-yank.webm)

This video shows how to get into normal mode (Ctrl+Shift+Space) and move to the
text that is to be yanked (`y`) into the clipboard.
Mind, the clipboard is pasted with newlines stripped off (`<Shift-p>`).

Note how the status line at the bottom reflects the current input mode.

## Line marks

Ever wanted to jump quickly to the top of the previous prompt?
With a little bit of shell integration, you can make the shell tell
the terminal which lines in your screen and scrollback buffer to remember.

### Setting a line mark

This is what a shell integration would do, but you can simply
mark lines yourself by trivially writing to `stdout`, as follows:

```sh
printf "\033]133;A\033\\"
```

This will tell the terminal to remember the line as a jump-target. Being a prompt mark, it also
starts a new command block; to mark a line without that, see
[Bookmarking a line in Vi normal mode](#bookmarking-a-line-in-vi-normal-mode).

`OSC 133 ; A` is the shell-integration sequence for "a new prompt starts here". Contour also
accepted a dedicated `SETMARK` sequence (`CSI > M`) for this until 0.8.0, when it was removed in
favour of this one.

### Jump via shortcut

Ensure you have a similar configuration set as follows to relatively jump
up or down of your marked lines.

```yaml
input_mapping:
    - { mods: [Control, Alt], key: K, action: ScrollMarkUp,   mode: "~Alt" }
    - { mods: [Control, Alt], key: J, action: ScrollMarkDown, mode: "~Alt" }
```

### Jump extension to Vi-like Normal Mode

Use `[m` and `]m` to jump to the next line mark up and down when being in
normal input mode.

### Bookmarking a line in Vi normal mode

In Vi-like normal mode, `mm` toggles a *user mark* on the line the cursor is on. A user mark is a
bookmark, not a prompt: `[m`/`]m`, the `im`/`am` text objects and `ScrollMarkUp`/`ScrollMarkDown`
visit user marks and prompt marks alike, but a user mark never starts a command block, so it neither
splits a fold nor decides where old output is dropped from the scrollback.

A user mark is drawn as ◆ in the gutter and as a tick on the scrollbar; see
[Command blocks](demo/command-blocks.md#user-marks).

### Line marks as text objects

In Vi-like normal mode, you can span a text object in between two line marks,
as follows:

- `vim` - visual select within two line marks (excluding marked lines)
- `vam` - visual select around two line marks (including marked line)
- `yim` - yank within two line marks (excluding marked lines)
- `yam` - yank around two line marks (including marked line)
