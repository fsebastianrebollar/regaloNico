# La vitrina de Nico

Firmware para una Raspberry Pi Pico 2 W (RP2350) que controla la vitrina del
trofeo de campeones del mundo: luces, base giratoria, dos botones y una web de
control con temática de fútbol.

## Hardware

| Pin  | Conexión                                        |
|------|-------------------------------------------------|
| GP16 | Datos de la tira WS2815 (6 LEDs, 12 V)          |
| GP18 | DRV8835 PHASE (sentido del motor)               |
| GP19 | DRV8835 ENABLE (PWM 20 kHz, velocidad)          |
| GP20 | DRV8835 MODE (fijo en alto)                     |
| GP14 | Botón 1 (a 3,3 V al pulsar): siguiente modo de luces |
| GP15 | Botón 2 (a 3,3 V al pulsar): escalera del motor |

Los dos botones a la vez durante 5 s borran la WiFi configurada, vuelven a la
red propia y reinician.

## WiFi

- **Red propia (por defecto):** red `cumplenico`; la web se abre en
  `http://cumplenico.com` (DNS propio + portal cautivo).
- **WiFi externa:** se configura desde la web (icono 🔧) y queda guardada en
  flash. Si la red no está, lo reintenta sin parar mientras todo lo demás
  funciona. La web va por la IP que asigne el router.

## Compilar y grabar

```sh
cp src/wifi_secrets.example.h src/wifi_secrets.h   # credenciales de la red propia
export PICO_SDK_PATH=~/pico/pico-sdk
export PICO_TOOLCHAIN_PATH=~/pico/toolchain/arm-gnu-toolchain-14.3.rel1-darwin-arm64-arm-none-eabi
cmake -S . -B build -G Ninja
ninja -C build && picotool load -v -x build/regalo_nico.uf2 -f
```

Requiere el Pico SDK 2.3.0. La web (`web/index.html`) se incrusta en el
firmware al compilar.

## Estructura

- `src/main.c`: arranque y bucle principal
- `src/wifi.c`: red propia / WiFi externa con reintentos
- `src/web_server.c`: servidor HTTP y API (`/api/state`, `/api/set`, `/api/preset`, `/api/button`, `/api/loco`, `/api/stop`)
- `src/control.c`: efectos de luces, rampa del motor, modos guardados, modo loco
- `src/settings.c`: ajustes persistentes en el último sector de la flash
- `src/buttons.c`, `src/mode_switch.c`: botones y vuelta a la red propia
- `lib/dhcpserver`, `lib/dnsserver`: de los ejemplos oficiales `pico-examples`
