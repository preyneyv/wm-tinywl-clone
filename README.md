# TinyWL Clone

This project is a hand-written clone of [TinyWL](https://gitlab.freedesktop.org/wlroots/wlroots/-/tree/master/tinywl) (CC0). The primary objective of this exercise is for me to understand how wayland works (so I can finally get back to [spatial-wl](https://www.github.com/preyneyv/spatial-wl)!)


## Running It

You'll need a copy of wlroots-0.19 built and set up. Depending on how you're doing this, you may want to set your `PKG_CONFIG_PATH` and `LD_LIBRARY_PATH` variables. Refer to `.env` for an example, if required.

```console
$ . .env
$ make run
```
