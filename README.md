# Modem série LoRa pour Ebyte E32-868T30 / E32-868T20

Firmware de remplacement pour les modules Ebyte E32 (STM8L151 + SX1276) : un modem
transparent UART ↔ LoRa, avec appairage (bind), accusés de réception et réémissions.

## Caractéristiques

- UART 9600 bauds 8N1 (PA2 = TX, PA3 = RX)
- LoRa 869,525 MHz, SF7, 250 kHz, CR 4/5 — sous-bande européenne 10 % / 500 mW
- Puissance :
  - **T30** (`modem.ihx`) : SX1276 à 14 dBm en entrée de l'ampli externe, soit ~27 dBm (≈ 500 mW, non mesuré)
  - **T20** (`modem_t20.ihx`) : SX1276 seul à 20 dBm (100 mW), maximum de la puce
- Paquets jusqu'à 120 octets, accusé de réception + jusqu'à 8 réémissions, suppression des doublons
- Écoute avant d'émettre, délai aléatoire anti-collision
- Tampon UART entrée 1 Ko, sortie 512 o ; AUX (PD0) bas quand le tampon d'entrée est presque plein
- Débit utile mesuré : ~600 octets/s

Bannière au démarrage (après 5 s) :

```
Yolo TMTC lora 868 RX/TX 500mW 9600 - SX127x v0x12 OK - NET 680D
```

## LED (M0 = PC5) et bouton (M1 = PC6)

| État | LED |
|---|---|
| Démarrage normal | 4 clignotements lents |
| Paquet émis / reçu | flash court |
| Mode bind | clignotement rapide |
| Bind réussi | fixe jusqu'au redémarrage |

**Bind :** mettre sous tension les deux modules avec M1 à la masse. Ils s'échangent leurs
identifiants, enregistrent un numéro de réseau commun en EEPROM, puis la LED reste fixe.
Redémarrer les deux modules.

## Compilation

[SDCC](https://sdcc.sourceforge.net/) 4.5 :

```
sdcc -mstm8 --opt-code-size --out-fmt-ihx -o modem.ihx main.c                    # E32-868T30
sdcc -mstm8 --opt-code-size --out-fmt-ihx -DMODEL_T20 -o modem_t20.ihx main.c   # E32-868T20
```

## Flashage (ST-Link V2, SWIM)

Câblage : SWIM et RST sur les deux petits pads du module, plus GND et 3,3 V.

```
openocd -f interface/stlink.cfg -c "adapter speed 363" -f target/stm8l.cfg \
  -c "reset_config srst_only connect_assert_srst" \
  -c "init; halt; load_image modem.ihx; reset run; shutdown"
```

Les deux modules doivent avoir la même version du firmware. Un T20 et un T30 peuvent
communiquer ensemble : seule la puissance change.

## Fichiers

- `main.c` — firmware du modem
- `modem.ihx` — image compilée pour le T30
- `modem_t20.ihx` — image compilée pour le T20
- `uartprobe.c` — firmware de test pour trouver les broches UART
- `tools/stress.ps1` — test de charge entre deux ports COM (PowerShell)

## Limites

- Le firmware ne fait pas respecter la limite de 10 % du temps d'émission.
- Deux rafales simultanées dans les deux sens dépassent le débit radio : utiliser le
  contrôle de flux (AUX → CTS) ou limiter le débit côté PC.
- Brochage déduit du firmware d'origine d'un T30 ; AUX sur PD0 non vérifié. Le brochage
  du T20 est supposé identique.
