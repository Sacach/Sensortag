/*
 * Tekijat: Santtu Orava ja Casimir Saastamoinen
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <ti/mw/display/Display.h>

/* XDCtools Header files */
#include <xdc/std.h>
#include <xdc/runtime/System.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/drivers/I2C.h>
#include <ti/drivers/i2c/I2CCC26XX.h>
#include <ti/drivers/PIN.h>
#include <ti/drivers/pin/PINCC26XX.h>
#include <ti/mw/display/DisplayExt.h>

/* Board Header files */
#include "Board.h"
#include "sensors/bmp280.h"
#include "sensors/mpu9250.h"
#include "wireless/comm_lib.h"

#define STACKSIZE 2048
Char taskStack[STACKSIZE];
Char commTaskStack[STACKSIZE];

// Globaaleja muuttujia
char komentomerkkijono[16] = "event:UP";
char payload[16];
int Tila = 0, Siirto = 0, Loppu = -1, siirrot = 0, Tyhjennys = 1;

//MPU pinnien maaritys
static PIN_Handle hMpuPin;
static PIN_State MpuPinState;
static PIN_Config MpuPinConfig[] = {
    Board_MPU_POWER  | PIN_GPIO_OUTPUT_EN | PIN_GPIO_HIGH | PIN_PUSHPULL | PIN_DRVSTR_MAX,
    PIN_TERMINATE
};

//nappien maaritys
static PIN_Handle buttonHandle;
static PIN_Handle buttonHandle1;

static PIN_State buttonState;

//ledin maaritys
static PIN_Handle ledHandle;
static PIN_State ledState;

// Nappien setuppi

PIN_Config buttonConfig[] = {
   Board_BUTTON0  | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
   PIN_TERMINATE
};

PIN_Config buttonConfig1[] = {
   Board_BUTTON1  | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
   PIN_TERMINATE
};

// LED setuppi //

PIN_Config ledConfig[] = {
   Board_LED0 | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
   PIN_TERMINATE
};

// Nappi funktio //

void buttonFxn(PIN_Handle handle, PIN_Id pinId) {
	if (Tila != 0){
		int RANDOM;
		// Nappi 1 painetaan:
		// Valo syttyy/sammuu
		// Lahetetaan viesti taustajarjestelmalle
		// Tilan asetus
		// ja siirtojen lasku
		if(pinId == Board_BUTTON0) {
			PIN_setOutputValue(ledHandle, Board_LED0, !PIN_getOutputValue( Board_LED0 ));

			if (!strcmp(komentomerkkijono, "event:LEFT\n")){
				Siirto = 1;
			}
			else if (!strcmp(komentomerkkijono, "event:RIGHT\n")){
				Siirto = 2;
			}
			else if (!strcmp(komentomerkkijono, "event:UP\n")){
				Siirto = 3;
			}
			else if (!strcmp(komentomerkkijono, "event:DOWN\n")){
				Siirto = 4;
			}
			Send6LoWPAN(0x1234, komentomerkkijono, strlen(komentomerkkijono));
			sprintf(komentomerkkijono, "NEUTRAL");
			siirrot += 1;
			/*
			System_printf("DONE\n");
			System_flush();
			*/
			StartReceive6LoWPAN();
		   }

		// Nappi 2 (Liike satunnaiseen suuntaan)

		else if(pinId == Board_BUTTON1) {

			PIN_setOutputValue(ledHandle, Board_LED0, !PIN_getOutputValue( Board_LED0 ));
			srand(time(0));

			RANDOM = rand() % 4;
			if (RANDOM == 0){
				Siirto = 1;
				sprintf(komentomerkkijono, "event:LEFT\n");
			}
			else if (RANDOM == 1){
				Siirto = 2;
				sprintf(komentomerkkijono, "event:RIGHT\n");
			}
			else if (RANDOM == 2){
				Siirto = 3;
				sprintf(komentomerkkijono, "event:UP\n");
			}
			else if (RANDOM == 3){
				Siirto = 4;
				sprintf(komentomerkkijono, "event:DOWN\n");
			}
			Send6LoWPAN(0x1234, komentomerkkijono, strlen(komentomerkkijono));
			sprintf(komentomerkkijono, "NEUTRAL");
			siirrot += 1;
			/*
			System_printf("rand\n");
			System_flush();
			*/
			StartReceive6LoWPAN();
		}
	}
	else {
		Tila = 5;
	}
}

// ******************************
//
// MPU USES ITS OWN I2C INTERFACE
//
// ******************************
static const I2CCC26XX_I2CPinCfg i2cMPUCfg = {
    .pinSDA = Board_I2C0_SDA1,
    .pinSCL = Board_I2C0_SCL1
};

// Viestien vastaanotto
Void commTaskFxn(UArg arg0, UArg arg1){

	uint16_t senderAddr;

	int32_t result = StartReceive6LoWPAN();
	if(result != true) {
		System_abort("Wireless receive start failed");
	}

	while(true){
		if (GetRXFlag()) {
			memset(payload,0,16);
			Receive6LoWPAN(&senderAddr, payload, 16);
			/*
			System_printf(payload);
			System_flush();
			*/
			// Siirtyy voittotilaan
			if(!strcmp(payload,"67,WIN")){
				Loppu = 1;
			}
			// Siirtyy haviotilaan
			if(!strcmp(payload,"67,LOST GAME")){
				Loppu = 0;
			}
		}
	}
}

Void sensorFxn(UArg arg0, UArg arg1) {

	//taskin muuttujat
	float ax, ay, az, gx, gy, gz;
	int luettu_data;
	char siirto_tulostus[20];
	int i = 0;
    // i2c-muuttujat

	I2C_Handle i2cMPU;
	I2C_Params i2cMPUParams;

	// i2c alustus

    I2C_Params_init(&i2cMPUParams);
    i2cMPUParams.bitRate = I2C_400kHz;
    i2cMPUParams.custom = (uintptr_t)&i2cMPUCfg;

    Display_Params params;
    Display_Params_init(&params);
    params.lineClearMode = DISPLAY_CLEAR_BOTH;

    // Naytto kayttoon ohjelmassa
    Display_Handle displayHandle = Display_open(Display_Type_LCD, &params);

    // MPU OPEN I2C

    i2cMPU = I2C_open(Board_I2C, &i2cMPUParams);
    if (i2cMPU == NULL) {
        System_abort("Error Initializing I2CMPU\n");
    }

    // MPU POWER ON

    PIN_setOutputValue(hMpuPin,Board_MPU_POWER, Board_MPU_POWER_ON);

    // WAIT 100MS FOR THE SENSOR TO POWER UP
	Task_sleep(100000 / Clock_tickPeriod);
    System_printf("MPU9250: Power ON\n");
    System_flush();

    // MPU9250 SETUP
    // ja tilannepaivitys naytolle

	System_printf("MPU9250: Setup and calibration...\n");
	System_flush();
	Display_print0(displayHandle, 4, 0, "Odota!");
	Display_print0(displayHandle, 5, 0, "Kalibroidaan");
	Display_print0(displayHandle, 6, 0, "laitetta...");

	mpu9250_setup(&i2cMPU);

	// Tilannepaivitys naytolle
	System_printf("MPU9250: Setup and calibration OK\n");
	System_flush();
	Display_clear(displayHandle);
	Display_print0(displayHandle, 5, 0, "Kalibrointi ok");
	Display_print0(displayHandle, 6, 0, "Paina nappia");

    // MPU CLOSE I2C

    I2C_close(i2cMPU);

    //RTOS muuttujat grafiikkaa varten
    tContext *pContext = DisplayExt_getGrlibContext(displayHandle);

    //looppi
	while (1) {

	    // MPU OPEN I2C

	    i2cMPU = I2C_open(Board_I2C, &i2cMPUParams);
	    if (i2cMPU == NULL) {
	        System_abort("Error Initializing I2CMPU\n");
	    }

	    //liikkumisohjeet naytolle

	    if (Tila == 5){
	    	if (Tyhjennys == 1){
	    		Display_clear(displayHandle);
	    		Tyhjennys = 0;
	    	}
	    	Display_print0(displayHandle, 0, 0, "Kallista");
	    	Display_print0(displayHandle, 1, 0, "laitetta ja");
			Display_print0(displayHandle, 2, 0, "odota ohjetta");

			Display_print0(displayHandle, 6, 5, "TAI");

			Display_print0(displayHandle, 10, 5, "Satunnainen");
			Display_print0(displayHandle, 11, 5, "liike");

			//"nuoli" ylanappiin
			GrLineDraw(pContext,85,5,90,0);
			GrLineDraw(pContext,90,0,95,5);
			GrLineDraw(pContext,95,5,85,5);

			//"nuoli" alanappiin
			GrLineDraw(pContext,85,90,90,95);
			GrLineDraw(pContext,90,95,95,90);
			GrLineDraw(pContext,95,90,85,90);

			// Piirto puskurista naytolle
			GrFlush(pContext);
	    }

	    //Kertoo mihin suuntaan liikuttiin

	    if (Siirto != 0){
			if (Siirto == 1){
				Display_clear(displayHandle);
				Display_print0(displayHandle, 5, 0, "Liikuit");
				Display_print0(displayHandle, 6, 6, "vasemmalle");
				Tyhjennys = 1;
				Tila = 5;
				Siirto = 0;
			}
			else if (Siirto == 2){
				Display_clear(displayHandle);
				Display_print0(displayHandle, 5, 0, "Liikuit oikealle");
				Tyhjennys = 1;
				Tila = 5;
				Siirto = 0;
			}
			else if (Siirto == 3){
				Display_clear(displayHandle);
				Display_print0(displayHandle, 5, 0, "Liikuit ylos");
				Tyhjennys = 1;
				Tila = 5;
				Siirto = 0;
			}
			else if (Siirto == 4){
				Display_clear(displayHandle);
				Display_print0(displayHandle, 5, 0, "Liikuit alas");
				Tyhjennys = 1;
				Tila = 5;
				Siirto = 0;
			}
	    }

	    // MPU ASK DATA
	    // ja sijoitus luettu_data muuttujaan

		luettu_data = mpu9250_get_data(&i2cMPU, &ax, &ay, &az, &gx, &gy, &gz);

		// Datan kasittely

		if (Tila != 0){
			if(luettu_data == 1){
				Display_clear(displayHandle);
				sprintf(komentomerkkijono, "event:LEFT\n");
				System_printf(komentomerkkijono);
				Display_print0(displayHandle, 0, 0, "VASEN");
				Display_print0(displayHandle, 1, 0, "Paina nappia");

				Display_print0(displayHandle, 6, 5, "TAI");

				Display_print0(displayHandle, 10, 5, "Satunnainen");
				Display_print0(displayHandle, 11, 5, "liike");

				GrLineDraw(pContext,85,5,90,0);
				GrLineDraw(pContext,90,0,95,5);
				GrLineDraw(pContext,95,5,85,5);

				GrLineDraw(pContext,85,90,90,95);
				GrLineDraw(pContext,90,95,95,90);
				GrLineDraw(pContext,95,90,85,90);
				// Piirto puskurista naytolle
				GrFlush(pContext);
				System_flush();
				Tila = 1;
				Tyhjennys = 1;
			}

			else if(luettu_data == 2){
				Display_clear(displayHandle);
				sprintf(komentomerkkijono, "event:RIGHT\n");
				System_printf(komentomerkkijono);
				Display_print0(displayHandle, 0, 0, "OIKEA");
				Display_print0(displayHandle, 1, 0, "Paina nappia");

				Display_print0(displayHandle, 6, 5, "TAI");

				Display_print0(displayHandle, 10, 5, "Satunnainen");
				Display_print0(displayHandle, 11, 5, "liike");

				GrLineDraw(pContext,85,5,90,0);
				GrLineDraw(pContext,90,0,95,5);
				GrLineDraw(pContext,95,5,85,5);

				GrLineDraw(pContext,85,90,90,95);
				GrLineDraw(pContext,90,95,95,90);
				GrLineDraw(pContext,95,90,85,90);
				// Piirto puskurista naytolle
				GrFlush(pContext);
				System_flush();
				Tila = 2;
				Tyhjennys = 1;
			}

			else if(luettu_data == 3){
				Display_clear(displayHandle);
				sprintf(komentomerkkijono, "event:UP\n");
				System_printf(komentomerkkijono);
				Display_print0(displayHandle, 0, 0, "YLOS");
				Display_print0(displayHandle, 1, 0, "Paina nappia");

				Display_print0(displayHandle, 6, 5, "TAI");

				Display_print0(displayHandle, 10, 5, "Satunnainen");
				Display_print0(displayHandle, 11, 5, "liike");

				GrLineDraw(pContext,85,5,90,0);
				GrLineDraw(pContext,90,0,95,5);
				GrLineDraw(pContext,95,5,85,5);

				GrLineDraw(pContext,85,90,90,95);
				GrLineDraw(pContext,90,95,95,90);
				GrLineDraw(pContext,95,90,85,90);
				// Piirto puskurista naytolle
				GrFlush(pContext);
				System_flush();
				Tila = 3;
				Tyhjennys = 1;
			}

			else if(luettu_data == 4){
				Display_clear(displayHandle);
				sprintf(komentomerkkijono, "event:DOWN\n");
				System_printf(komentomerkkijono);
				Display_print0(displayHandle, 0, 0, "ALAS");
				Display_print0(displayHandle, 1, 0, "Paina nappia");

				Display_print0(displayHandle, 6, 5, "TAI");

				Display_print0(displayHandle, 10, 5, "Satunnainen");
				Display_print0(displayHandle, 11, 5, "liike");

				GrLineDraw(pContext,85,5,90,0);
				GrLineDraw(pContext,90,0,95,5);
				GrLineDraw(pContext,95,5,85,5);

				GrLineDraw(pContext,85,90,90,95);
				GrLineDraw(pContext,90,95,95,90);
				GrLineDraw(pContext,95,90,85,90);
				// Piirto puskurista naytolle
				GrFlush(pContext);
				System_flush();
				Tila = 4;
				Tyhjennys = 1;
			}


			// Voiton tulostus naytolle
			// ja muuttujien alustus uutta pelia varten
			// ja tanssivat tikku ukot
			if (Loppu == 1){
				while(i<10){
					if (i % 2 == 0){
						Display_clear(displayHandle);
						//tikku ukko 1
						//paa
						GrCircleDraw(pContext,20,8,3);
						//torso
						GrLineDraw(pContext,20,12,20,19);
						GrLineDraw(pContext,16,15,24,15);
						//kadet
						GrLineDraw(pContext,16,15,12,19);
						GrLineDraw(pContext,24,15,28,11);
						//jalat
						GrLineDraw(pContext,19,20,19,26);
						GrLineDraw(pContext,21,20,21,26);

						//tikku ukko 2
						//paa
						GrCircleDraw(pContext,36,8,3);
						//torso
						GrLineDraw(pContext,36,12,36,19);
						GrLineDraw(pContext,32,15,40,15);
						//kadet
						GrLineDraw(pContext,32,15,28,11);
						GrLineDraw(pContext,40,15,44,19);
						//jalat
						GrLineDraw(pContext,35,20,35,26);
						GrLineDraw(pContext,37,20,37,26);
					}
					else{
						Display_clear(displayHandle);

						//tikku ukko 1
						//paa
						GrCircleDraw(pContext,20,8,3);
						//torso
						GrLineDraw(pContext,20,12,20,19);
						GrLineDraw(pContext,16,15,24,15);
						//kadet
						GrLineDraw(pContext,16,15,12,11);
						GrLineDraw(pContext,24,15,28,19);
						//jalat
						GrLineDraw(pContext,19,20,19,26);
						GrLineDraw(pContext,21,20,21,26);

						//tikku ukko 2
						//paa
						GrCircleDraw(pContext,36,8,3);
						//torso
						GrLineDraw(pContext,36,12,36,19);
						GrLineDraw(pContext,32,15,40,15);
						//kadet
						GrLineDraw(pContext,32,15,28,19);
						GrLineDraw(pContext,40,15,44,11);
						//jalat
						GrLineDraw(pContext,35,20,35,26);
						GrLineDraw(pContext,37,20,37,26);
					}
					Display_print0(displayHandle, 4, 0, "VICTORY");
					sprintf(siirto_tulostus, "teit %d siirtoa", siirrot);
					Display_print0(displayHandle, 5, 0, siirto_tulostus);
					Display_print0(displayHandle, 6, 0, "Uusi peli?");
					Display_print0(displayHandle, 7, 0, "Paina nappia!");
					i += 1;
					Task_sleep(500000 / Clock_tickPeriod);
				}
				i = 0;
				siirrot = 0;
				Tila = 0;
				Loppu = -1;
				luettu_data = 0;
			}
			// Havion tulostus naytolle
			// ja muuttujien alustus uutta pelia varten
			else if (Loppu == 0){
				Display_clear(displayHandle);
				Display_print0(displayHandle, 4, 0, "DEFEAT");
				//tukee "vain" 9 999 999 siirtoon asti
				sprintf(siirto_tulostus, "teit %d siirtoa", siirrot);
				Display_print0(displayHandle, 5, 0, siirto_tulostus);
				Display_print0(displayHandle, 6, 0, "Uusi peli?");
				Display_print0(displayHandle, 7, 0, "Paina nappia!");
				siirrot = 0;
				Tila = 0;
				Loppu = -1;
				luettu_data = 0;
			}
		}

	    // MPU CLOSE I2C
	    I2C_close(i2cMPU);

	    // WAIT 1s
    	Task_sleep(1000000 / Clock_tickPeriod);
	}

	// MPU9250 POWER OFF
	// Because of loop forever, code never goes here
    PIN_setOutputValue(hMpuPin,Board_MPU_POWER, Board_MPU_POWER_OFF);
}

int main(void) {
	//taskien parametrit

	Task_Handle task;
	Task_Params taskParams;

	Task_Handle commTask;
	Task_Params commTaskParams;

	//Alustus
    Board_initGeneral();

    //Vayla mukaan ohjelmaan
    Board_initI2C();

    // OPEN MPU POWER PIN

    hMpuPin = PIN_open(&MpuPinState, MpuPinConfig);
    if (hMpuPin == NULL) {
    	System_abort("Pin open failed!");
    }

    //Napit kayttoon ohjelmassa

    buttonHandle = PIN_open(&buttonState, buttonConfig);
    if(!buttonHandle) {
    	System_abort("Error initializing button pins\n");
    }

    buttonHandle1 = PIN_open(&buttonState, buttonConfig1);
	if(!buttonHandle1) {
		System_abort("Error initializing button pins\n");
	}

	//Ledi kayttoon ohjelmassa

    ledHandle = PIN_open(&ledState, ledConfig);
    if(!ledHandle) {
    	System_abort("Error initializing LED pins\n");
    }

    // Asetetaan painonapille keskeytyksen käsittelijä

    if (PIN_registerIntCb(buttonHandle, &buttonFxn) != 0) {
    	System_abort("Error registering button callback function");
    }

    //Taski kayttoon ohjelmassa
    Task_Params_init(&taskParams);
    taskParams.stackSize = STACKSIZE;
    taskParams.stack = &taskStack;
    taskParams.priority=2;

    task = Task_create((Task_FuncPtr)sensorFxn, &taskParams, NULL);
    if (task == NULL) {
    	System_abort("Task create failed!");
    }

    //KommunikointiTaski kaytoon ohjelmassa
    Init6LoWPAN();
    Task_Params_init(&commTaskParams);
	commTaskParams.stackSize = STACKSIZE;
	commTaskParams.stack = &commTaskStack;
	commTaskParams.priority=1;

	commTask = Task_create(commTaskFxn, &commTaskParams, NULL);
	if (commTask == NULL) {
		System_abort("Task create failed!");
	}

    /* Start BIOS */
    BIOS_start();

    return (0);
}
