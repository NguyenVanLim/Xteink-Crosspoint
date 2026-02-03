import os
from PIL import Image

def process_image(filepath):
    try:
        img = Image.open(filepath).convert('1') # Convert to 1-bit monochrome
        width, height = img.size
        
        # Invert if necessary (assuming black icon on white background, and we want 1=black for drawing)
        # GfxRenderer drawPixel: state=true -> Clear bit (White?), state=false -> Set bit (Black?)
        # GfxRenderer.cpp: 
        #   if (state) frameBuffer[byteIndex] &= ~(1 << bitPosition); // Clear bit
        #   else frameBuffer[byteIndex] |= 1 << bitPosition; // Set bit
        # Wait. 
        # drawPixel(x, y, true) -> Clears bit.
        # drawPixel(x, y, false) -> Sets bit.
        # Usually 0=Black, 1=White for E-ink? Or 0=White?
        # Standard E-ink: 1=White, 0=Black (or vice versa depending on logic).
        # Let's check GfxRenderer text rendering: 
        # drawText ... const bool black = true ... renders with `black` state.
        # renderChar ... calls drawPixel(x, y, black).
        # So `drawPixel(..., true)` draws 'Black' color (conceptually).
        # But implementation: `if (state) Clear bit`. `else Set bit`.
        # So `true` (Color) implies Clearing a bit -> 0?
        # And `false` implies Setting a bit -> 1?
        # If Background is White (0xFF, all 1s?), then Clearing bit (0) makes it Black.
        # So 0 = Black, 1 = White.
        # So `drawPixel(..., true)` makes it 0 (Black).
        
        # My Icons: Black (RGB 0,0,0) on Transparent/White.
        # I want "Black" pixels to trigger `drawPixel(..., true)`.
        # Image.convert('1'): 0=Black, 255=White.
        # So if pixel is 0 (Black), I want bit=1 (to indicate "draw this").
        # If pixel is 255 (White), I want bit=0 (skip).
        
        data = []
        byte = 0
        bit_idx = 0
        
        # Scan row by row
        for y in range(height):
            for x in range(width):
                pixel = img.getpixel((x, y))
                # pixel is 0 for black, 255 for white
                if pixel == 0: # Black -> we want to draw it
                     byte |= (1 << (7 - bit_idx))
                
                bit_idx += 1
                if bit_idx == 8:
                    data.append(byte)
                    byte = 0
                    bit_idx = 0
            
            # Pad row to byte boundary?
            # Standard GFX libs usually pad rows. My helper will parse bit stream or row-aligned?
            # Easiest is row-aligned for width not % 8.
            if bit_idx > 0:
                data.append(byte)
                byte = 0
                bit_idx = 0
                
        return data, width, height
    except Exception as e:
        print(f"Error processing {filepath}: {e}")
        return None, 0, 0

images_p = "/Users/nguyenvanlim/Documents/Xteink/crosspoint-reader-vi/src/images"
files = {
    "icon_books": "icon_book.png", # User renamed it
    "icon_files": "icon_files.png",
    "icon_network": "icon_network.png",
    "icon_settings": "icon_settings.png"
}

print("#pragma once")
print("#include <cstdint>\n")

for name, filename in files.items():
    path = os.path.join(images_p, filename)
    data, w, h = process_image(path)
    if data:
        print(f"// {name}: {w}x{h}")
        print(f"const uint8_t {name}[] = {{")
        hex_data = [f"0x{b:02x}" for b in data]
        # split into lines of 12 bytes
        for i in range(0, len(hex_data), 12):
            print("    " + ", ".join(hex_data[i:i+12]) + ",")
        print("};\n")
