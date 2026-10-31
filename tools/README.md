# Ferramentas

## Emulador da tela (`emu/`)

Compila o código de desenho do firmware no PC, com stubs no lugar do hardware (`emu/stubs/`), e salva capturas da tela de 320×240 em `emu/shots/*.ppm`. A tela é um `GFXcanvas16` da Adafruit GFX de verdade. O áudio também roda (32 amostras por ms emulado), então o osciloscópio e o medidor mostram sinal.

1. Gere o sketch já preprocessado, com os protótipos que o Arduino cria:

   ```
   cd tools/emu
   arduino-cli compile --fqbn "rp2040:rp2040:rpipico:usbstack=tinyusb" --preprocess ../../firmware/tobri_synth_firmware_touchscreen_5 > sketch_pp.cpp
   ```

2. Compile com qualquer compilador C++17. Aqui foi usado o `zig c++` (`pip install ziglang`). Troque `<GFX>` pela pasta da biblioteca Adafruit GFX:

   ```
   python -m ziglang c++ -std=gnu++17 -O1 -w -Istubs -I<GFX> -o emu main.cpp <GFX>/Adafruit_GFX.cpp
   ```

3. Rode `./emu` dentro de `tools/emu`. Crie antes as pastas `shots/` e `png/`.

Para mudar quais telas são capturadas, edite `main.cpp`. Ele chama `switchPage()`, simula toques (`tap`, `touch`) e notas (`onNoteOn`).

## PDFs

Os scripts precisam de `reportlab`, `pillow` e `pymupdf`, e usam as fontes Segoe UI e Consolas do Windows.

```
python sheets.py emu                          # converte as capturas para PNG (emu/png)
python manual.py ../docs/Tobri_Synth_Manual.pdf
python wiring.py ../docs/Tobri_Synth_Ligacoes.pdf
```
