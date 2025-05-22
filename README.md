# vim-niri-nav

**[jasoncarloscox.com/creations/vim-sway-nav](https://jasoncarloscox.com/creations/vim-sway-nav/)**

Seamless navigation between [niri](https://github.com/YaLTeR/niri) windows and (Neo)Vim splits with the same key bindings.

This is a modified version of [jasoncarloscox.com/creations/vim-sway-nav](https://jasoncarloscox.com/creations/vim-sway-nav/) which
in turn was Inspired by [vim-tmux-navigator](https://github.com/christoomey/vim-tmux-navigator).

## Requirements

- Vim built with `+clientserver` (check with `vim --version`), or Neovim
- [jq](https://github.com/stedolan/jq)
- (optional) [`timeout` from the GNU coreutils](https://www.gnu.org/software/coreutils/timeout) -- see the _Configuration_ section below

## Installation

First, install this repository as a Vim plugin. For example, if you use [vim-plug](https://github.com/junegunn/vim-plug):

```
Plug 'https://git.sr.ht/~jcc/vim-sway-nav'
```

Second, add a symbolic link to the `vim-sway-nav` shell script somewhere on your `$PATH`. Where the script is found depends on how you installed the plugin, but adding a link should look something like the following:

```
ln -s ~/path/to/vim-sway-nav/vim-sway-nav ~/.local/bin/vim-sway-nav
```

Finally, modify your Sway config to use `vim-sway-nav` instead of your normal `focus left`, `focus down`, etc. bindings:

```
bindsym $mod+$left exec vim-sway-nav left
bindsym $mod+$down exec vim-sway-nav down
bindsym $mod+$up exec vim-sway-nav up
bindsym $mod+$right exec vim-sway-nav right
```

You can now use `$mod+<arrow>` to navigate among Sway windows and Vim splits!

## Configuration

The `vim-sway-nav` shell script applies a timeout to its communication with (Neo)Vim and falls back to normal `swaymsg focus <direction>` commands if the timeout is exceeded. This is useful in cases where (Neo)Vim is blocked on some long-running operation and takes a long time to respond -- better to at least move to the adjacent Sway window than to do nothing.

The timeout is implemented using the [`timeout` program from the GNU coreutils](https://www.gnu.org/software/coreutils/timeout). If the `timeout` program is not available, no timeout will be applied. The default timeout is `0.1s` (1/10th of a second), but this can be overridden by setting the `VIM_SWAY_NAV_TIMEOUT` environment variable. Setting this variable to `0` will disable the timeout behavior, allowing (Neo)Vim to be as slow as it wants to be.

## Contributing

Contributions are welcome! You can send questions, bug reports, patches, etc. by email to [~jcc/public-inbox@lists.sr.ht](https://lists.sr.ht/~jcc/public-inbox). (Don't know how to contribute via email? Check out the interactive tutorial at [git-send-email.io](https://git-send-email.io), or [email me](mailto:me@jasoncarloscox.com) for help.)
