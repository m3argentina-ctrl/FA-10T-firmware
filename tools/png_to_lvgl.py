"""Convertir PNG → array C en formato LVGL 8.x (TRUE_COLOR RGB565 big-endian).

El proyecto usa CONFIG_LV_COLOR_DEPTH=16 y CONFIG_LV_COLOR_16_SWAP=y, así que
cada pixel se serializa como 2 bytes big-endian (high byte primero).

Uso: python png_to_lvgl.py <input.png> <output.c> <var_name> [max_w] [max_h]

Si se especifica max_w/max_h, la imagen se redimensiona manteniendo proporción
(thumbnail). El fondo blanco se preserva: las imágenes se usarán sobre fondos
claros o dentro de contenedores blancos.
"""
import sys
from PIL import Image

def convert(input_png, output_c, var_name, max_w=None, max_h=None):
    img = Image.open(input_png).convert("RGB")
    if max_w or max_h:
        img.thumbnail((max_w or img.width, max_h or img.height), Image.LANCZOS)
    w, h = img.width, img.height
    pixels = list(img.getdata())

    bytes_out = []
    for r, g, b in pixels:
        rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        # Big-endian (LV_COLOR_16_SWAP=1): high byte primero.
        bytes_out.append((rgb565 >> 8) & 0xFF)
        bytes_out.append(rgb565 & 0xFF)

    with open(output_c, 'w', encoding='utf-8') as f:
        f.write('// Auto-generado por tools/png_to_lvgl.py — NO editar a mano.\n')
        f.write(f'// Fuente: {input_png}, {w}x{h}, RGB565 big-endian.\n\n')
        f.write('#include "lvgl.h"\n\n')
        f.write(f'static const uint8_t {var_name}_map[] = {{\n')
        for i in range(0, len(bytes_out), 16):
            chunk = bytes_out[i:i+16]
            f.write('    ' + ', '.join(f'0x{b:02X}' for b in chunk) + ',\n')
        f.write('};\n\n')
        f.write(f'const lv_img_dsc_t {var_name} = {{\n')
        f.write('    .header = {\n')
        f.write('        .always_zero = 0,\n')
        f.write(f'        .w = {w},\n')
        f.write(f'        .h = {h},\n')
        f.write('        .cf = LV_IMG_CF_TRUE_COLOR,\n')
        f.write('    },\n')
        f.write(f'    .data_size = sizeof({var_name}_map),\n')
        f.write(f'    .data = {var_name}_map,\n')
        f.write('};\n')

    print(f'Wrote {output_c}: {w}x{h} = {len(bytes_out)} bytes')

if __name__ == '__main__':
    if len(sys.argv) < 4:
        print('Usage: python png_to_lvgl.py <input.png> <output.c> <var_name> [max_w] [max_h]')
        sys.exit(1)
    input_png  = sys.argv[1]
    output_c   = sys.argv[2]
    var_name   = sys.argv[3]
    max_w      = int(sys.argv[4]) if len(sys.argv) > 4 else None
    max_h      = int(sys.argv[5]) if len(sys.argv) > 5 else None
    convert(input_png, output_c, var_name, max_w, max_h)
