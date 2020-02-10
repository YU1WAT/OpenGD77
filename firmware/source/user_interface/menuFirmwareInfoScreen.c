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
#include <user_interface/uiLocalisation.h>

static void updateScreen(void);
static void handleEvent(uiEvent_t *ev);

int menuFirmwareInfoScreen(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		updateScreen();
	}
	else
	{
		if (ev->hasEvent)
			handleEvent(ev);
	}
	return 0;
}

static void updateScreen(void)
{
	char buf[17];

	snprintf(buf, 16, "v1.38.20", GITVERSION);
	buf[11] = 0; // git hash id 7 char long;

	ucClearBuf();
	ucPrintCentered(5, "OpenGD77", FONT_8x16);
	ucPrintCentered(24, "Built by NA7Q", FONT_8x8);
	ucPrintCentered(34,__TIME__, FONT_8x8);
	ucPrintCentered(44,__DATE__, FONT_8x8);
	ucPrintCentered(54, buf, FONT_8x8);
	ucRender();
	displayLightTrigger();
}


static void handleEvent(uiEvent_t *ev)
{
	if (KEYCHECK_PRESS(ev->keys,KEY_RED))
	{
		menuSystemPopPreviousMenu();
		return;
	}
	else if (KEYCHECK_PRESS(ev->keys,KEY_GREEN))
	{
		menuSystemPopAllAndDisplayRootMenu();
		return;
	}
}
