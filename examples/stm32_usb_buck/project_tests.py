"""Project checks for the STM32 USB buck benchmark."""

from __future__ import annotations

import volt

from .schematic_symbols import DISPLAY_REFERENCES


BOARD_REFERENCES = (
    "VIN_SRC",
    "PWR/J",
    "PWR/F1",
    "PWR/FB1",
    "PWR/U5",
    "PWR/L1",
    "PWR/DSW",
    "PWR/U3V3",
    "PWR/CIN",
    "PWR/CBST",
    "PWR/C5V",
    "PWR/REN_TOP",
    "PWR/REN_BOT",
    "PWR/RFB_TOP",
    "PWR/RFB_BOT",
    "PWR/C3V3",
    "PWR/FBVDDA",
    "PWR/CVDDA",
    "USB/J1",
    "USB/U1",
    "SUPPORT/CVDD",
    "SUPPORT/CVCAP1",
    "SUPPORT/CVCAP2",
    "SUPPORT/RRESET",
    "SUPPORT/RBOOT",
    "SUPPORT/SWBOOT",
    "SUPPORT/Y1",
    "SUPPORT/CHSEIN",
    "SUPPORT/CHSEOUT",
    "LED_STATUS/R",
    "LED_STATUS/D",
    "U1",
    "J2",
    "J3",
    "H1",
    "H2",
    "H3",
    "H4",
)


def register_project_tests(project: volt.Project) -> None:
    @project.design.test
    def buck_input_and_rails_are_preserved(check) -> None:
        check.net("+12V").connects("VIN_SRC.OUT")
        check.net("PWR/IN_12V").connects("PWR/J.1", "PWR/F1.1")
        check.net("PWR/FUSED_12V").connects("PWR/F1.2", "PWR/FB1.1")
        check.net("PWR/BUCK_IN").connects(
            "PWR/FB1.2",
            "PWR/U5.IN",
            "PWR/CIN.1",
            "PWR/REN_TOP.1",
        )
        check.net("PWR/BUCK_EN").connects(
            "PWR/U5.EN",
            "PWR/REN_TOP.2",
            "PWR/REN_BOT.1",
        )
        check.net("PWR/BUCK_SW").connects(
            "PWR/U5.SW",
            "PWR/DSW.K",
            "PWR/L1.1",
            "PWR/CBST.1",
        )
        check.net("PWR/BUCK_BST").connects("PWR/U5.BST", "PWR/CBST.2")
        check.net("PWR/OUT_5V").connects(
            "PWR/L1.2",
            "PWR/C5V.1",
            "PWR/RFB_TOP.1",
            "PWR/U3V3.VI",
        )
        check.net("PWR/BUCK_FB").connects(
            "PWR/U5.FB",
            "PWR/RFB_TOP.2",
            "PWR/RFB_BOT.1",
        )
        check.net("PWR/OUT_3V3").connects("PWR/U3V3.VO", "PWR/C3V3.1", "PWR/FBVDDA.1")
        check.net("PWR/VDDA").connects("PWR/FBVDDA.2", "PWR/CVDDA.1")
        check.net("PWR/GND").connects(
            "PWR/J.2",
            "PWR/J.3",
            "PWR/J.4",
            "PWR/CIN.2",
            "PWR/U5.GND",
            "PWR/REN_BOT.2",
            "PWR/DSW.A",
            "PWR/C5V.2",
            "PWR/RFB_BOT.2",
            "PWR/U3V3.GND",
            "PWR/C3V3.2",
            "PWR/CVDDA.2",
        )
        check.net("+3V3").connects("U1.VBAT", "U1.VDD", "J2.VTref", "J3.1")
        check.net("VDDA").connects("U1.VDDA")
        check.net("GND").connects("VIN_SRC.GND", "U1.VSS", "U1.VSSA")
        check.net("GND").connects("H1.1", "H2.1", "H3.1", "H4.1")
        check.net("SUPPORT/VDD").connects(
            "SUPPORT/CVDD.1",
            "SUPPORT/RRESET.1",
            "SUPPORT/SWBOOT.A",
        )
        check.net("SUPPORT/GND").connects(
            "SUPPORT/CVDD.2",
            "SUPPORT/CVCAP1.2",
            "SUPPORT/CVCAP2.2",
            "SUPPORT/RBOOT.2",
        )
        check.net("SUPPORT/VCAP_1").connects("SUPPORT/CVCAP1.1")
        check.net("SUPPORT/VCAP_2").connects("SUPPORT/CVCAP2.1")

    @project.design.test
    def usb_debug_and_user_io_are_preserved(check) -> None:
        check.net("USB/VBUS").connects("USB/J1.VBUS", "USB/U1.VBUS")
        check.net("USB/USB_DP").connects("USB/J1.D+", "USB/U1.I/O1")
        check.net("USB/USB_DM").connects("USB/J1.D-", "USB/U1.I/O2")
        check.net("USB/MCU_USB_DP").connects("USB/U1.I/O4")
        check.net("USB/MCU_USB_DM").connects("USB/U1.I/O3")
        check.net("MCU_USB_DP").connects("U1.PA12")
        check.net("MCU_USB_DM").connects("U1.PA11")
        check.net("USB/GND").connects("USB/J1.GND", "USB/J1.Shield", "USB/U1.GND")
        check.net("SWDIO").connects("J2.SWDIO", "U1.PA13")
        check.net("SWCLK").connects("J2.SWCLK", "U1.PA14")
        check.net("SWO").connects("J2.SWO", "U1.PB3")
        check.net("NRST").connects("J2.nRESET", "U1.NRST")
        check.net("BOOT0").connects("J2.TDI", "J3.2", "U1.BOOT0")
        check.net("STATUS_LED").connects("U1.PC13")
        check.net("LED_STATUS/SUPPLY").connects("LED_STATUS/R.1")
        check.net("LED_STATUS/LED_A").connects("LED_STATUS/R.2", "LED_STATUS/D.A")
        check.net("LED_STATUS/SIGNAL").connects("LED_STATUS/D.K")

    @project.design.test
    def power_and_signal_domains_stay_separate(check) -> None:
        for power_net in ("+12V", "+5V", "+3V3", "VDDA"):
            check.no_connection(power_net, "GND")
        check.no_connection("+3V3", "VDDA")
        check.no_connection("USB/USB_DP", "USB/USB_DM")
        check.no_connection("MCU_USB_DP", "MCU_USB_DM")
        check.no_connection("SWDIO", "SWCLK")
        check.no_connection("BOOT0", "GND")
        check.no_connection("STATUS_LED", "GND")

    @project.schematic.test
    def schematic_places_displayed_benchmark_parts(check) -> None:
        check.places(*DISPLAY_REFERENCES)

    @project.board.test
    def pcb_places_and_routes_benchmark_parts(check) -> None:
        check.has_outline()
        check.places(*BOARD_REFERENCES)
