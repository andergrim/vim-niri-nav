# vim-niri-nav

**[https://github.com/andergrim/vim-niri-nav](https://github.com/andergrim/vim-niri-nav)**

Seamless navigation between [niri](https://github.com/YaLTeR/niri) windows and (Neo)Vim splits with the same key bindings.

This is a modified version of [vim-sway-nav](https://jasoncarloscox.com/creations/vim-sway-nav/) which
in turn was inspired by [vim-tmux-navigator](https://github.com/christoomey/vim-tmux-navigator).

## Requirements

- Vim built with `+clientserver` (check with `vim --version`), or Neovim
- [jq](https://github.com/stedolan/jq)
- (optional) [`timeout` from the GNU coreutils](https://www.gnu.org/software/coreutils/timeout) -- see the _Configuration_ section below

## Installation

First, install this repository as a Vim plugin. For example, if you use [vim-plug](https://github.com/junegunn/vim-plug):

```
Plug 'https://github.com/andergrim/vim-niri-nav'
```

Second, add a symbolic link to the `vim-niri-nav` shell script somewhere on your `$PATH`. Where the script is found depends on how you installed the plugin, but adding a link should look something like the following:

```
ln -s ~/path/to/vim-niri-nav/vim-niri-nav ~/.local/bin/vim-niri-nav
```

Finally, modify your niri config to use `vim-niri-nav` instead of your normal `focus-column-left`, `focus-window-down`, etc. bindings:

```
Mod+Left      { spawn "vim-niri-nav" "left"; }
Mod+Down      { spawn "vim-niri-nav" "down"; }
Mod+Up        { spawn "vim-niri-nav" "up"; }
Mod+Right     { spawn "vim-niri-nav" "right"; }
```

You can now use `Mod+<arrow>` to navigate among niri windows and Vim splits!


## Configuration

The `vim-niri-nav` shell script applies a timeout to its communication with (Neo)Vim and falls back to normal `niri msg action focus-<type + direction>` commands if the timeout is exceeded. This is useful in cases where (Neo)Vim is blocked on some long-running operation and takes a long time to respond -- better to at least move to the adjacent niri window than to do nothing.

The timeout is implemented using the [`timeout` program from the GNU coreutils](https://www.gnu.org/software/coreutils/timeout). If the `timeout` program is not available, no timeout will be applied. The default timeout is `0.1s` (1/10th of a second), but this can be overridden by setting the `VIM_NIRI_NAV_TIMEOUT` environment variable. Setting this variable to `0` will disable the timeout behavior, allowing (Neo)Vim to be as slow as it wants to be.

### niri focus-window-or-workspace-[down|up] support

If you want to use `focus-window-or-workspace-[down|up]` instead of `focus-window-[down|up]`:

Set `g:vim_niri_nav_workspace` to `"true"` in your vim configuration:

```
let g:vim_niri_nav_workspace = "true"
```

Then, in your niri config add the `w` parameter to the `vim-niri-nav` command:

```
Mod+Down    { spawn "vim-niri-nav" "up" "w"; }
```

## Contributing

Contributions are welcome! You can send questions, bug reports, and pull requests through Github.
