/*
 * Copyright (C)2019 Roger Clark. VK3KYY / G4KYF
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <user_interface/menuSystem.h>
#include <user_interface/uiUtilities.h>
#include <user_interface/uiLocalisation.h>
#include "fw_calibration.h"
#include "fw_settings.h"
#include "fw_trx.h"

static calibrationRSSIMeter_t rssiCalibration;
static void updateScreen(void);
static void handleEvent(uiEvent_t *ev);

int menuRSSIScreen(uiEvent_t *ev, bool isFirstRun)
{
	static uint32_t m = 0;

	if (isFirstRun)
	{
		calibrationGetRSSIMeterParams(&rssiCalibration);
	}
	else
	{
		if (ev->hasEvent)
			handleEvent(ev);

		if((ev->time - m) > RSSI_UPDATE_COUNTER_RELOAD)
		{
			m = ev->time;
			updateScreen();
		}
	}
	return 0;
}


static void updateScreen(void)
{
	int dBm;
	int barGraphLength;
	char buffer[17];

		if (trxCurrentBand[TRX_RX_FREQ_BAND] == RADIO_BAND_UHF)
		{
			// Use fixed point maths to scale the RSSI value to dBm, based on data from VK4JWT and VK7ZJA
			dBm = -151 + trxRxSignal;// Note no the RSSI value on UHF does not need to be scaled like it does on VHF
		}
		else
		{
			// VHF
			// Use fixed point maths to scale the RSSI value to dBm, based on data from VK4JWT and VK7ZJA
			dBm = -164 + ((trxRxSignal * 32) / 27);
		}

		ucClearBuf();
		menuDisplayTitle(currentLanguage->rssi);

		sprintf(buffer, "%d", trxRxSignal);
		ucPrintCore(0, 3, buffer, FONT_8x8, TEXT_ALIGN_RIGHT, false);
		
		// Display "No Signal" when signal is lost
		if (dBm <= -151)
		{
		sprintf(buffer, "No Signal");
		}
		else
		{
		sprintf(buffer, "%d%s", dBm, "dBm");
		}
		
		ucPrintCentered(20, buffer, FONT_8x16);

		barGraphLength = ((dBm + 130) * 24)/10;
		if (barGraphLength<0)
		{
			barGraphLength=0;
		}

		if (barGraphLength>123)
		{
			barGraphLength=123;
		}
		ucFillRect(4, 40,barGraphLength,8,false);

		ucPrintCore(5,50,"S1  S3  S5  S7  S9", FONT_6x8, TEXT_ALIGN_LEFT, false);
		ucRender();
		trxRxSignal=0;

}


static void handleEvent(uiEvent_t *ev)
{
	if (KEYCHECK_SHORTUP(ev->keys,KEY_RED))
	{
		menuSystemPopPreviousMenu();
		return;
	}
	else if (KEYCHECK_SHORTUP(ev->keys,KEY_GREEN))
	{
		menuSystemPopAllAndDisplayRootMenu();
		return;
	}

	displayLightTrigger();
}
