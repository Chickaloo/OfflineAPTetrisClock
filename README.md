# Offline AP TetrisClock
Adaptation of Brian Lough's original Wifi Clock. You can check it out here: https://github.com/witnessmenow/WiFi-Tetris-Clock

Because this was intended to be a gift, I could not have the clock rely on having an access point to talk to (because the credentials are hard to adjust without expertise). Thus, this version is a fully offline clock that hosts a web page that allows your client to "set" the clock.

# Build your own

Looking to build your own? Here is the parts list I used! Please be ready to solder.

| Part | Link | Notes |
|---|---|---|
| TinyPico | https://www.crowdsupply.com/unexpected-maker/tinypico | One of the smallest boards available. You can adapt this project to any board, but this one has... an "ecosystem" around it and requires less expertise. |
| Matrix Shield | https://www.tindie.com/products/brianlough/tinypico-matrix-shield/ | Connects your TinyPico to your display. You don't "need" it, but this project is so much easier with this available. It does not come assembled; be prepared to do that yourself. |
| 64x32 Display | https://www.amazon.com/gp/product/B07F2JW8D3/ref=ppx_yo_dt_b_asin_title_o03_s00?ie=UTF8&psc=1 | Generic Chinese RGB Matrix. Uh, you can get it way cheaper on Aliexpress or something. But this is the one I used. |
| USB 2.0 Micro B | https://www.amazon.com/gp/product/B07232M876/ref=ppx_yo_dt_b_asin_title_o01_s01?ie=UTF8&psc=1 | You need this to program the Tiny Pico. |
| 5V 5A Power Supply | https://www.amazon.com/gp/product/B078RT3ZPS/ref=ppx_yo_dt_b_asin_title_o01_s01?ie=UTF8&psc=1 | The USB cable doesn't provide enough juice to drive the display, so you need this. You can use any amperage above 5A, as long as it's 5V. |

TECHNICALLY, this is all you need to build one, but because the display doesn't come with a frame, you'll have to figure out how to build one yourself.
