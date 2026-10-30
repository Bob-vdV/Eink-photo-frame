# Eink photo frame

## Hardware

**Display**     Seeed 7.3-inch ePaper - Spectra 6 800x480
**Board**       XIAO ePaper Display Board(nRF52840) - EN04

## Installation


1. Get arduino
2. Add Seeed library
3. Put board into bootloader mode (double click reset)
4. Push to board in arduino

5. Get your image: 
```bash
uv run src/einkdisplay/spectra6_converter.py images/2/cropped.jpg --raw images/2/raw.bin

uv run src/einkdisplay/upload_image.py images/2/raw.bin 
```

## Cool links


[Image to C array converter (not suitable for spectra 6 display :(](https://projedefteri.com/en/tools/lcd-assistant/)
