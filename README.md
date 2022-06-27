# ESP32-M125

This is hardware and software designed to be fitted within a Marsed M-125 set of medical scales.

Examples of modifications with much older prototype boards https://youtu.be/l1VAymhwtVMand  https://youtu.be/jy-5EB_Qu8Y

![M125 09 51 44](https://user-images.githubusercontent.com/996983/175944987-87cbbf7c-26f5-4192-81ba-7a809adc3ed9.png)

Takes data when SEND pressed and reports via MQTT and https. But can also work with an NFC board https://github.com/revk/ESP32-PN532 so tapping a card or tag will report ID and weight, pressing SEND for you.
