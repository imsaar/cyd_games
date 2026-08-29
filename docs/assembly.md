# CYD Arcade Handheld — Assembly Guide

This guide walks through building a battery-powered, hand-held arcade unit around a **CYD board** (ESP32-2432S028, the "Cheap Yellow Display" — 2.8" resistive-touch TFT with SD slot) inside a 3D-printed case. Photos are from two builds (a grey case and a red case); the wiring is identical in both.

## Bill of materials

- 3D-printed two-part case (with a battery bay, a slide-switch cutout, and a buzzer pocket) — [CYD Cheap Yellow Display Portable Case w/ Battery](https://makerworld.com/en/models/853122-cyd-cheap-yellow-display-portable-case-w-battery)
- CYD board — ESP32-2432S028 2.8" TFT touch display
- [USB-C LiPo charge/boost combo board](https://www.aliexpress.us/item/3256805424127849.html) (micro-USB or USB-C in, `B+`/`B-` battery pads, boost converter out)
- [103450 3.7V 2000mAh Polymer Lithium Rechargeable Battery](https://www.aliexpress.us/item/3256811607466964.html) (pictured: LD1D 103450)
- Small piezo buzzer
- 6 mm slide micro-switch (3-pin, trimmed to 2-pin — used as the power on/off switch)
- Hookup wire — red (power) and black (ground) at minimum, plus a few extra colors for the buzzer leads
- 4x M3 x 6mm screws (fasten the CYD board to the case)
- 4x M3 x 20mm screws (fasten the two case halves together)
- Blue painter's tape (temporary strain relief while soldering)
- Hot glue or UV-cure resin
- Heat-shrink tubing

## Tools

- Soldering iron, solder, flux
- Wire cutters/strippers, tweezers, small pliers
- Rotary tool / hobby knife (to trim the printed case for the charge board)
- Small Phillips screwdriver
- Computer + USB-C cable (to flash the games firmware)

---

## 1. Start with the case and the CYD board

![Start with case and CYD](../CYD_Arcade_Assembly/1%20Start%20with%20case%20and%20CYD.jpeg)

Lay out the printed case (base + lid) and the CYD board (still in its anti-static box). Dry-fit the board against the base to see where the display, USB ports, and mounting posts land before cutting anything.

## 2. Cut the case to fit the charge/boost board

![Cut to accommodate charge boost](../CYD_Arcade_Assembly/2%20Cut%20to%20accomodate%20charge%20boost.jpeg)

The case's molded mount for the charge/boost board is slightly undersized. Carefully trim/grind the plastic post in the battery bay until the board will drop in flush.

## 3. Post cut

![Post cut](../CYD_Arcade_Assembly/3%20Post%20cut.jpeg)

The mount after trimming — note the thin plastic rail lying in the bay. That rail acts as a spacer that keeps the LiPo cell from sliding into the switch/charge-board area later.

## 4. Connect the charge board and boost converter

![Connect charge and boost](../CYD_Arcade_Assembly/4%20connect%20charge%20and%20boost.jpeg)

Test-fit the charge/boost combo board (USB-C input, `B+`/`B-` battery pads, boost converter output) into the trimmed mount. Don't glue it yet — just confirm the fit and connector clearances.

## 5. Glue the charge/boost board in place

![Glue charge boost](../CYD_Arcade_Assembly/5%20glue%20charge%20boost.jpeg)

Once the fit is confirmed, secure the board with a few dabs of hot glue or UV resin at the corners so it can't shift once the battery is loaded on top of it.

## 6. Add the battery

![Add battery](../CYD_Arcade_Assembly/6%20add%20battery.jpeg)

Solder/plug the LiPo's red/black leads into the charge board's battery input, then lay the cell into the battery bay alongside the plastic spacer rail from step 3.

## 7. Flash the CYD with the games firmware

![Flash CYD games](../CYD_Arcade_Assembly/7%20flash%20cyd%20games.jpeg)

Before final wiring, connect the bare CYD board to a computer over USB-C/micro-USB and flash it with this repo's firmware. It's much easier to re-flash now than after the board is buried in wiring and screwed into the case.

## 8. Prep the buzzer

![Buzzer](../CYD_Arcade_Assembly/8%20buzzer.jpeg)

Solder leads onto the piezo buzzer's two terminals ahead of time so it's ready to plug into the board.

## 9. Connect the buzzer

![Connect buzzer](../CYD_Arcade_Assembly/9%20connect%20buzzer.jpeg)
![Another way to connect buzzer](../CYD_Arcade_Assembly/9%20another%20way%20to%20connect%20buzzer.jpeg)

Wire the buzzer to the CYD board's speaker header. Two options are shown: soldering leads directly to the header pins, or using the board's JST-style connector if your buzzer has a matching plug — either works, pick whichever matches the hardware you have.

## 10. Place the CYD board in the case

![Place CYD in case](../CYD_Arcade_Assembly/10%20place%20CYD%20in%20case.jpeg)

Seat the buzzer into its pocket in the case, then lower the CYD board on top so the buzzer sits underneath it, with the display facing the case's window.

## 11. Fasten the CYD board

![CYD fastened](../CYD_Arcade_Assembly/11%20CYD%20fastened.jpeg)

Screw the board down at its four corner mounting holes. Dress the buzzer and power wires so they run cleanly toward the battery bay and don't get pinched under the board.

## 12. Confirm the battery wiring

![Battery connected](../CYD_Arcade_Assembly/12%20battery%20connected.jpeg)

Both builds (grey and red case) at the same stage: battery seated in its bay, wired to the charge/boost board, with the JST connector wrapped in a bit of painter's tape for strain relief.

## 13. Both boards fastened

![CYD fastened](../CYD_Arcade_Assembly/13%20CYD%20fastened%20.jpeg)

Side-by-side of the grey and red builds with their CYD boards fastened and buzzer/power wiring routed — a good checkpoint to compare your wiring against before moving on to the power switch.

## 14. The power switch

![6mm micro switch](../CYD_Arcade_Assembly/14%206mm%20micro%20switch.jpeg)

The power switch is a small 6 mm slide micro-switch with 3 pins (common + two throws). We only need simple on/off, so it needs one modification before it'll fit the case.

## 15. Trim one leg of the switch

![Cut one leg of switch](../CYD_Arcade_Assembly/15%20cut%20one%20leg%20of%20switch.jpeg)

Clip off the unused third pin with cutters so the switch behaves as a simple 2-terminal inline switch and fits its narrow slot in the case.

## 16. Place the switch in the case

![Place switch in case](../CYD_Arcade_Assembly/16%20place%20switch%20in%20case.jpeg)

Drop the trimmed switch into its molded slot next to the charge/boost board.

## 17. Seat the switch

![Switch seated](../CYD_Arcade_Assembly/17%20switch%20seated.jpeg)

Press the switch fully home so it sits flush and its two remaining legs are accessible for soldering.

## 18. Solder the switch wire

![Solder switch wire](../CYD_Arcade_Assembly/18%20solder%20switch%20wire.jpeg)

Solder the battery/boost-board's positive lead to one leg of the switch, inserting the switch in series on the power rail so it can cut power to the CYD board.

## 19. Wire switch output — power and ground

![Output power and ground](../CYD_Arcade_Assembly/19%20output%20power%20and%20ground.jpeg)

From the switch's other leg and the boost board's ground, run a red (power out) and black (ground) wire toward the CYD board. Tape them down temporarily for strain relief while you finish soldering.

## 20. Solder the ground contact on the CYD board

![Solder CYD ground contact](../CYD_Arcade_Assembly/20%20solder%20cyd%20ground%20contact.jpeg)

On the underside of the CYD board, solder the black ground wire to the ground pad near the USB connectors.

## 21. Solder the power-in contact on the CYD board

![Solder CYD power in contact](../CYD_Arcade_Assembly/21%20solder%20cyd%20power%20in%20contact.jpeg)

Solder the red power wire to the adjacent 5V/VIN pad. This feeds the board from the battery/boost circuit in parallel with its USB input, so it can run untethered.

## 22. All soldered

![All soldered](../CYD_Arcade_Assembly/22%20all%20soldered.jpeg)

Both power leads landed on the board, buzzer wiring bundled with heat-shrink, everything dressed away from the USB ports and screw posts.

## 23. Final check before closing up

![Soldered and done before closing and screwing](../CYD_Arcade_Assembly/23%20soldered%20and%20done%20before%20closing%20and%20screwing.jpeg)

Before closing the case:

- Flip the power switch and confirm the display lights up on battery power
- Confirm the buzzer sounds
- Confirm charging works by plugging a USB-C cable into the charge board
- Tuck all wiring clear of the lid's screw posts

Once everything checks out, close the case and screw the lid down. The build is complete.
