# Daydream2HID: Unplanned Non-obsolescence

Daydream2HID turns a Google Daydream controller into a USB HID device.

# Demo Video

<video controls>
   <source src="https://hardfault.life/public/daydream-controller/demo.webm" type="video/webm">
</video>

*Note that I've improved the gyro code a bit since recording this video.*

# Usage

Pairing:

1. Flash the firmware onto your board of choice, following the steps [below](#building).
2. Plug the board into your PC over USB. Once plugged in, one LED should turn
   on, and a second one should start flashing.
3. Press and hold the home (circle) button on the Daydream controller to wake it
   up.
4. The LED on the board will stop flashing. The controller is now paired!

Controls:

| Button | Action |
|:------ |:------ |
| Trackpad click | Left-click |
| App button | Right-click |
| Volume up | Scroll up |
| Volume down | Scroll down |
| Home button | Enable gyro mouse control |

The Daydream controller's trackpad works the way you'd expect. Move your finger
around on it to move the mouse around. Press down on the trackpad to left-click.
You can click-and-drag with the trackpad, too. The trackpad supports
edge-dragging.

To use the controller in gyro mode, press and hold the Home (circle) button,
and wave the controller around. This will cause the cursor to move around, sort
of like a Wii-mote.

![Picture of the Daydream controller with buttons labeling each button and the gyro axes](/misc/controller-axis.png)

# Building

I developed this firmware using an [nRF52840 DK]. You should be able to use any
board [supported by Zephyr] as long as it has Bluetooth, USB, and a couple
LEDs. You'll have to add your own overlay file in the `boards/` directory if
you aren't using an nRF52840 DK.

If you haven't used Zephyr before, make sure you've followed the steps to set up
the [Zephyr SDK].

First, clone the repo and prepare a Zephyr build environment:

```bash
git clone git@github.com:ryukoposting/daydream2hid.git
cd daydream2hid
west init .
west config zephyr.base $(pwd)

# Prepare a python virtual env for builds
python3 -m venv .venv
. .venv/bin/activate
pip install west
pip install -r zephyr/scripts/requirements.txt

# Finish setting up Zephyr
west update
west zephyr-export
```
Then, tell West what board you're using, and build!

```bash
west config build.board nrf52840dk/nrf52840
west build -p
```

Then, flash the board:

```bash
west flash
```

[Zephyr SDK]: https://docs.zephyrproject.org/latest/develop/getting_started/index.html#install-the-zephyr-sdk
[supported by Zephyr]: https://docs.zephyrproject.org/latest/boards/index.html#
[nRF52840 DK]: https://docs.zephyrproject.org/latest/boards/nordic/nrf52840dk/doc/index.html

