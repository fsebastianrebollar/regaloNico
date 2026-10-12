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

## Alexa

Con WiFi externa, la vitrina se anuncia como una bombilla Philips Hue llamada
**España** (no hace falta skill). En la app de Alexa: *Dispositivos > + >
Añadir dispositivo > Luz > Philips Hue* o simplemente "Alexa, busca
dispositivos".

- "Alexa, enciende España" → ¡gol! (bandera y la copa girando 7 s)
- "Alexa, apaga España" → lo corta

## Actualizaciones

La flash está dividida en dos particiones (A/B): la versión nueva se escribe en
la que no se usa y arranca en modo "prueba antes de quedarte"; si no llega a
arrancar bien, la Pico vuelve sola a la anterior. Los ajustes (WiFi, modos)
están fuera de las particiones y se conservan.

- **Desde GitHub:** con WiFi externa, la vitrina mira cada 6 h si hay versión
  nueva (y al conectarse). Solo se avisa en la web, en 🔧 > Actualización, y
  se instala desde ahí.
- **A mano:** en 🔧 > Actualización > *Subir archivo .uf2* (funciona también
  en la red propia).

Para publicar una versión (sube `VERSION`, compila y la deja en la rama
`firmware`):

```sh
tools/publicar.sh
```

Preparar una Pico nueva (una sola vez, por USB): tabla de particiones y luego
el firmware.

```sh
picotool partition create partitions.json build/pt.uf2
picotool load build/pt.uf2 -f && picotool reboot -u
picotool load -x build/regalo_nico.uf2
```

## Compilar y grabar

```sh
cp src/wifi_secrets.example.h src/wifi_secrets.h   # credenciales de la red propia
export PICO_SDK_PATH=~/pico/pico-sdk
export PICO_TOOLCHAIN_PATH=~/pico/toolchain/arm-gnu-toolchain-14.3.rel1-darwin-arm64-arm-none-eabi
cmake -S . -B build -G Ninja
ninja -C build && picotool load -v -x build/regalo_nico.uf2 -f   # con la tabla de particiones ya grabada
```

Requiere el Pico SDK 2.3.0. La web (`web/index.html`) se incrusta en el
firmware al compilar.

## Estructura

- `src/main.c`: arranque y bucle principal
- `src/wifi.c`: red propia / WiFi externa con reintentos
- `src/web_server.c`: servidor HTTP y API (`/api/state`, `/api/set`, `/api/preset`, `/api/button`, `/api/loco`, `/api/stop`, `/api/wifi`, `/api/ota`, `/api/update`)
- `src/control.c`: efectos de luces, rampa del motor, modos guardados, modo loco
- `src/settings.c`: ajustes persistentes en el último sector de la flash
- `src/alexa.c`: emulación de Hue para Alexa (SSDP + API)
- `src/ota.c`, `src/https_get.c`: actualizaciones (GitHub por HTTPS y subida manual)
- `src/buttons.c`, `src/mode_switch.c`: botones y vuelta a la red propia
- `lib/dhcpserver`, `lib/dnsserver`: de los ejemplos oficiales `pico-examples`
