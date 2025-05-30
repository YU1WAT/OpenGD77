/*
 * Copyright (C)2019 Kai Ludwig, DG4KLU
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

#include <AT1846S.h>
#include <calibration.h>
#include <HR-C6000.h>
#include <settings.h>
#include <trx.h>
#include <user_interface/menuSystem.h>
#include <user_interface/uiUtilities.h>


uint8_t trx_css_measure_count = 0;
int trx_measure_count = 0;
volatile bool trxTransmissionEnabled = false;
volatile bool trxIsTransmitting = false;
uint32_t trxTalkGroupOrPcId = 9;// Set to local TG just in case there is some problem with it not being loaded
uint32_t trxDMRID = 0;// Set ID to 0. Not sure if its valid. This value needs to be loaded from the codeplug.
int txstopdelay = 0;
volatile bool trxIsTransmittingTone = false;
static uint16_t txPower;
static bool analogSignalReceived = false;
static bool digitalSignalReceived = false;

int trxCurrentBand[2] = {RADIO_BAND_VHF,RADIO_BAND_VHF};// Rx and Tx band.

#if USE_DATASHEET_RANGES
const frequencyBand_t RADIO_FREQUENCY_BANDS[RADIO_BANDS_TOTAL_NUM] =  {
													{
														.minFreq=13400000,
														.maxFreq=17400000
													},// VHF
													{
														.minFreq=20000000,
														.maxFreq=26000000
													},// 220Mhz
													{
														.minFreq=40000000,
														.maxFreq=52000000
													}// UHF
};
#else
const frequencyBand_t RADIO_FREQUENCY_BANDS[RADIO_BANDS_TOTAL_NUM] =  {
													{
														.calTableMinFreq = 13400000,
														.minFreq=12700000,
														.maxFreq=17800000
													},// VHF
													{
														.calTableMinFreq = 13400000,
														.minFreq=19000000,
														.maxFreq=28200000
													},// 220Mhz
													{
														.calTableMinFreq = 40000000,
														.minFreq=38000000,
														.maxFreq=56400000
													}// UHF
};
#endif

static const int TRX_SQUELCH_MAX = 70;
const uint8_t TRX_NUM_CTCSS = 50;
const uint16_t TRX_CTCSSTones[] = {
	 670,  693,  719,  744,  770,  797,  825,  854,  885,  915,
	 948,  974, 1000, 1035, 1072, 1109, 1148, 1188, 1230, 1273,
	1318, 1365, 1413, 1462, 1514, 1567, 1598, 1622, 1655, 1679,
	1713, 1738, 1773, 1799, 1835, 1862, 1899, 1928, 1966, 1995,
	2035, 2065, 2107, 2181, 2257, 2291, 2336, 2418, 2503, 2541
};
const uint16_t TRX_DCS_TONE = 13440;  // 134.4Hz is the data rate of the DCS bitstream (and a reason not to use that tone for CTCSS)
const uint8_t TRX_NUM_DCS = 83;
// There are other possible codes but these are standard and there are mathematical reasons for why they are standard.
// The CPS supports another larger set of codes. The code should work if one of these is programmed, but FPP doesn't support setting.
const uint16_t TRX_DCSCodes[] = {
	0023, 0025, 0026, 0031, 0032, 0043, 0047, 0051, 0054, 0065, 0071, 0072, 0073, 0074,
	0114, 0115, 0116, 0125, 0131, 0132, 0134, 0143, 0152, 0155, 0156, 0162, 0165, 0172, 0174,
	0205, 0223, 0226, 0243, 0244, 0245, 0251, 0261, 0263, 0265, 0271,
	0306, 0311, 0315, 0331, 0343, 0345, 0351, 0364, 0365, 0371,
	0411, 0412, 0413, 0423, 0431, 0432, 0445, 0464, 0465, 0466,
	0503, 0506, 0516, 0532, 0546, 0565,
	0606, 0612, 0624, 0627, 0631, 0632, 0654, 0662, 0664,
	0703, 0712, 0723, 0731, 0732, 0734, 0743, 0754
};
// A DCS code has 04000 added to it to set the highest bit of the twelve encoded bits and is then Golay{23,12} encoded.
// This table is just the error correcting bits to save space because the rest of the bits are the 04000 plus the DCS code.
const uint16_t TRX_DCSECCBits[512] = {
	0x63a, 0x24f, 0x2a5, 0x6d0, 0x371, 0x704, 0x7ee, 0x39b,
	0x0d9, 0x4ac, 0x446, 0x033, 0x592, 0x1e7, 0x10d, 0x578,
	0x789, 0x3fc, 0x316, 0x763, 0x2c2, 0x6b7, 0x65d, 0x228,
	0x16a, 0x51f, 0x5f5, 0x180, 0x421, 0x054, 0x0be, 0x4cb,
	0x55c, 0x129, 0x1c3, 0x5b6, 0x017, 0x462, 0x488, 0x0fd,
	0x3bf, 0x7ca, 0x720, 0x355, 0x6f4, 0x281, 0x26b, 0x61e,
	0x4ef, 0x09a, 0x070, 0x405, 0x1a4, 0x5d1, 0x53b, 0x14e,
	0x20c, 0x679, 0x693, 0x2e6, 0x747, 0x332, 0x3d8, 0x7ad,
	0x0f6, 0x483, 0x469, 0x01c, 0x5bd, 0x1c8, 0x122, 0x557,
	0x615, 0x260, 0x28a, 0x6ff, 0x35e, 0x72b, 0x7c1, 0x3b4,
	0x145, 0x530, 0x5da, 0x1af, 0x40e, 0x07b, 0x091, 0x4e4,
	0x7a6, 0x3d3, 0x339, 0x74c, 0x2ed, 0x698, 0x672, 0x207,
	0x390, 0x7e5, 0x70f, 0x37a, 0x6db, 0x2ae, 0x244, 0x631,
	0x573, 0x106, 0x1ec, 0x599, 0x038, 0x44d, 0x4a7, 0x0d2,
	0x223, 0x656, 0x6bc, 0x2c9, 0x768, 0x31d, 0x3f7, 0x782,
	0x4c0, 0x0b5, 0x05f, 0x42a, 0x18b, 0x5fe, 0x514, 0x161,
	0x7d7, 0x3a2, 0x348, 0x73d, 0x29c, 0x6e9, 0x603, 0x276,
	0x134, 0x541, 0x5ab, 0x1de, 0x47f, 0x00a, 0x0e0, 0x495,
	0x664, 0x211, 0x2fb, 0x68e, 0x32f, 0x75a, 0x7b0, 0x3c5,
	0x087, 0x4f2, 0x418, 0x06d, 0x5cc, 0x1b9, 0x153, 0x526,
	0x4b1, 0x0c4, 0x02e, 0x45b, 0x1fa, 0x58f, 0x565, 0x110,
	0x252, 0x627, 0x6cd, 0x2b8, 0x719, 0x36c, 0x386, 0x7f3,
	0x502, 0x177, 0x19d, 0x5e8, 0x049, 0x43c, 0x4d6, 0x0a3,
	0x3e1, 0x794, 0x77e, 0x30b, 0x6aa, 0x2df, 0x235, 0x640,
	0x11b, 0x56e, 0x584, 0x1f1, 0x450, 0x025, 0x0cf, 0x4ba,
	0x7f8, 0x38d, 0x367, 0x712, 0x2b3, 0x6c6, 0x62c, 0x259,
	0x0a8, 0x4dd, 0x437, 0x042, 0x5e3, 0x196, 0x17c, 0x509,
	0x64b, 0x23e, 0x2d4, 0x6a1, 0x300, 0x775, 0x79f, 0x3ea,
	0x27d, 0x608, 0x6e2, 0x297, 0x736, 0x343, 0x3a9, 0x7dc,
	0x49e, 0x0eb, 0x001, 0x474, 0x1d5, 0x5a0, 0x54a, 0x13f,
	0x3ce, 0x7bb, 0x751, 0x324, 0x685, 0x2f0, 0x21a, 0x66f,
	0x52d, 0x158, 0x1b2, 0x5c7, 0x066, 0x413, 0x4f9, 0x08c,
	0x5e0, 0x195, 0x17f, 0x50a, 0x0ab, 0x4de, 0x434, 0x041,
	0x303, 0x776, 0x79c, 0x3e9, 0x648, 0x23d, 0x2d7, 0x6a2,
	0x453, 0x026, 0x0cc, 0x4b9, 0x118, 0x56d, 0x587, 0x1f2,
	0x2b0, 0x6c5, 0x62f, 0x25a, 0x7fb, 0x38e, 0x364, 0x711,
	0x686, 0x2f3, 0x219, 0x66c, 0x3cd, 0x7b8, 0x752, 0x327,
	0x065, 0x410, 0x4fa, 0x08f, 0x52e, 0x15b, 0x1b1, 0x5c4,
	0x735, 0x340, 0x3aa, 0x7df, 0x27e, 0x60b, 0x6e1, 0x294,
	0x1d6, 0x5a3, 0x549, 0x13c, 0x49d, 0x0e8, 0x002, 0x477,
	0x32c, 0x759, 0x7b3, 0x3c6, 0x667, 0x212, 0x2f8, 0x68d,
	0x5cf, 0x1ba, 0x150, 0x525, 0x084, 0x4f1, 0x41b, 0x06e,
	0x29f, 0x6ea, 0x600, 0x275, 0x7d4, 0x3a1, 0x34b, 0x73e,
	0x47c, 0x009, 0x0e3, 0x496, 0x137, 0x542, 0x5a8, 0x1dd,
	0x04a, 0x43f, 0x4d5, 0x0a0, 0x501, 0x174, 0x19e, 0x5eb,
	0x6a9, 0x2dc, 0x236, 0x643, 0x3e2, 0x797, 0x77d, 0x308,
	0x1f9, 0x58c, 0x566, 0x113, 0x4b2, 0x0c7, 0x02d, 0x458,
	0x71a, 0x36f, 0x385, 0x7f0, 0x251, 0x624, 0x6ce, 0x2bb,
	0x40d, 0x078, 0x092, 0x4e7, 0x146, 0x533, 0x5d9, 0x1ac,
	0x2ee, 0x69b, 0x671, 0x204, 0x7a5, 0x3d0, 0x33a, 0x74f,
	0x5be, 0x1cb, 0x121, 0x554, 0x0f5, 0x480, 0x46a, 0x01f,
	0x35d, 0x728, 0x7c2, 0x3b7, 0x616, 0x263, 0x289, 0x6fc,
	0x76b, 0x31e, 0x3f4, 0x781, 0x220, 0x655, 0x6bf, 0x2ca,
	0x188, 0x5fd, 0x517, 0x162, 0x4c3, 0x0b6, 0x05c, 0x429,
	0x6d8, 0x2ad, 0x247, 0x632, 0x393, 0x7e6, 0x70c, 0x379,
	0x03b, 0x44e, 0x4a4, 0x0d1, 0x570, 0x105, 0x1ef, 0x59a,
	0x2c1, 0x6b4, 0x65e, 0x22b, 0x78a, 0x3ff, 0x315, 0x760,
	0x422, 0x057, 0x0bd, 0x4c8, 0x169, 0x51c, 0x5f6, 0x183,
	0x372, 0x707, 0x7ed, 0x398, 0x639, 0x24c, 0x2a6, 0x6d3,
	0x591, 0x1e4, 0x10e, 0x57b, 0x0da, 0x4af, 0x445, 0x030,
	0x1a7, 0x5d2, 0x538, 0x14d, 0x4ec, 0x099, 0x073, 0x406,
	0x744, 0x331, 0x3db, 0x7ae, 0x20f, 0x67a, 0x690, 0x2e5,
	0x014, 0x461, 0x48b, 0x0fe, 0x55f, 0x12a, 0x1c0, 0x5b5,
	0x6f7, 0x282, 0x268, 0x61d, 0x3bc, 0x7c9, 0x723, 0x356,
};
static const int BAND_VHF_MIN 	= 14400000;
static const int BAND_VHF_MAX 	= 14800000;
static const int BAND_222_MIN 	= 22200000;
static const int BAND_222_MAX 	= 22500000;
static const int BAND_UHF_MIN 	= 42000000;
static const int BAND_UHF_MAX 	= 45000000;

enum CAL_DEV_TONE_INDEXES { CAL_DEV_DTMF = 0, CAL_DEV_TONE = 1, CAL_DEV_CTCSS_WIDE	= 2,CAL_DEV_CTCSS_NARROW = 3,CAL_DEV_DCS_WIDE = 4, CAL_DEV_DCS_NARROW	= 5};

static int currentMode = RADIO_MODE_NONE;
static bool currentBandWidthIs25kHz = BANDWIDTH_12P5KHZ;
static int currentRxFrequency = 14400000;
static int currentTxFrequency = 14400000;
static int currentCC = 1;
static uint8_t squelch = 0x00;
static bool rxCSSactive = false;

// AT-1846 native values for Rx
static uint8_t rx_fl_l;
static uint8_t rx_fl_h;
static uint8_t rx_fh_l;
static uint8_t rx_fh_h;

// AT-1846 native values for Tx
static uint8_t tx_fl_l;
static uint8_t tx_fl_h;
static uint8_t tx_fh_l;
static uint8_t tx_fh_h;

volatile uint8_t trxRxSignal;
volatile uint8_t trxRxNoise;
volatile uint8_t trxTxVox;
volatile uint8_t trxTxMic;

static uint8_t trxSaveVoiceGainTx = 0xff;
static uint16_t trxSaveDeviation = 0xff;
static uint8_t voice_gain_tx = 0x31; // default voice_gain_tx fro calibration, needs to be declared here in case calibration:OFF

int trxDMRMode = DMR_MODE_ACTIVE;// Active is for simplex
static volatile bool txPAEnabled = false;

static int trxCurrentDMRTimeSlot;

// DTMF Order: 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, A, B, C, D, *, #
const int trxDTMFfreq1[] = { 1336, 1209, 1336, 1477, 1209, 1336, 1477, 1209, 1336, 1477, 1633, 1633, 1633, 1633, 1209, 1477 };
const int trxDTMFfreq2[] = {  941,  697,  697,  697,  770,  770,  770,  852,  852,  852,  697,  770,  852,  941,  941,  941 };

calibrationPowerValues_t trxPowerSettings;

int	trxGetMode(void)
{
	return currentMode;
}

int	trxGetBandwidthIs25kHz(void)
{
	return currentBandWidthIs25kHz;
}

void trxSetModeAndBandwidth(int mode, bool bandwidthIs25kHz)
{
	if ((mode != currentMode) || (bandwidthIs25kHz != currentBandWidthIs25kHz))
	{
		currentMode = mode;

		currentBandWidthIs25kHz = bandwidthIs25kHz;

		taskENTER_CRITICAL();
		switch(mode)
		{
		case RADIO_MODE_NONE:
			// not truely off
			GPIO_PinWrite(GPIO_RX_audio_mux, Pin_RX_audio_mux, 0); // connect AT1846S audio to HR_C6000
			soundTerminateSound();
			terminate_digital();

			AT1846SetMode();
			AT1846SetBandwidth();
			trxUpdateC6000Calibration();
			trxUpdateAT1846SCalibration();
			break;
		case RADIO_MODE_ANALOG:
			GPIO_PinWrite(GPIO_TX_audio_mux, Pin_TX_audio_mux, 0); // Connect mic to mic input of AT-1846
			GPIO_PinWrite(GPIO_RX_audio_mux, Pin_RX_audio_mux, 1); // connect AT1846S audio to speaker
			//soundTerminateSound(); This does not seem to be necessary and also causes problems with the voice prompts
			terminate_digital();

			AT1846SetMode();
			AT1846SetBandwidth();
			trxUpdateC6000Calibration();
			trxUpdateAT1846SCalibration();
			break;
		case RADIO_MODE_DIGITAL:
			AT1846SetMode();// Also sets the bandwidth to 12.5kHz which is the standard for DMR
			trxUpdateC6000Calibration();
			trxUpdateAT1846SCalibration();
			GPIO_PinWrite(GPIO_TX_audio_mux, Pin_TX_audio_mux, 1); // Connect mic to MIC_P input of HR-C6000
			GPIO_PinWrite(GPIO_RX_audio_mux, Pin_RX_audio_mux, 0); // connect AT1846S audio to HR_C6000
			soundInit();
			init_digital();
			break;
		}
		taskEXIT_CRITICAL();
	}
}

int trxGetNextOrPrevBandFromFrequency(int frequency, bool nextBand)
{
	if (nextBand)
	{
		if (frequency > RADIO_FREQUENCY_BANDS[RADIO_BANDS_TOTAL_NUM - 1].maxFreq)
		{
			return 0; // First band
		}

		for(int band = 0; band < RADIO_BANDS_TOTAL_NUM - 1; band++)
		{
			if (frequency > RADIO_FREQUENCY_BANDS[band].maxFreq && frequency < RADIO_FREQUENCY_BANDS[band + 1].minFreq)
				return (band + 1); // Next band
		}
	}
	else
	{
		if (frequency < RADIO_FREQUENCY_BANDS[0].minFreq)
		{
			return (RADIO_BANDS_TOTAL_NUM - 1); // Last band
		}

		for(int band = 1; band < RADIO_BANDS_TOTAL_NUM; band++)
		{
			if (frequency < RADIO_FREQUENCY_BANDS[band].minFreq && frequency > RADIO_FREQUENCY_BANDS[band - 1].maxFreq)
				return (band - 1); // Prev band
		}
	}

	return -1;
}

int trxGetBandFromFrequency(int frequency)
{
	for(int i = 0; i < RADIO_BANDS_TOTAL_NUM; i++)
	{
		if ((frequency >= RADIO_FREQUENCY_BANDS[i].minFreq) && (frequency <= RADIO_FREQUENCY_BANDS[i].maxFreq))
		{
			return i;
		}
	}

	return -1;
}

bool trxCheckFrequencyInAmateurBand(int tmp_frequency)
{
	return ((tmp_frequency >= BAND_VHF_MIN) && (tmp_frequency <= BAND_VHF_MAX)) ||
			((tmp_frequency >= BAND_UHF_MIN) && (tmp_frequency <= BAND_UHF_MAX)) ||
			((tmp_frequency >= BAND_222_MIN) && (tmp_frequency <= BAND_222_MAX));
}

void trxReadVoxAndMicStrength(void)
{
	AT1846ReadVoxAndMicStrength();
}

void trxReadRSSIAndNoise(void)
{
	AT1846ReadRSSIAndNoise();
}

bool trxCarrierDetected(void)
{
	uint8_t squelch = 0;

	trxReadRSSIAndNoise();

	switch(currentMode)
	{
		case RADIO_MODE_NONE:
			break;

		case RADIO_MODE_ANALOG:
			if (currentChannelData->sql != 0)
			{
				squelch = TRX_SQUELCH_MAX - (((currentChannelData->sql - 1) * 11) >> 2);
			}
			else
			{
				squelch = TRX_SQUELCH_MAX - (((nonVolatileSettings.squelchDefaults[trxCurrentBand[TRX_RX_FREQ_BAND]]) * 11) >> 2);
			}
			break;

		case RADIO_MODE_DIGITAL:
			// Don't check for variable squelch, as some people seem to have this set to fully open on their DMR channels.
			/*
			if (currentChannelData->sql!=0)
			{
				squelch =  TRX_SQUELCH_MAX - (((currentChannelData->sql-1)*11)>>2);
			}
			else*/
			{
				squelch = TRX_SQUELCH_MAX - (((nonVolatileSettings.squelchDefaults[trxCurrentBand[TRX_RX_FREQ_BAND]]) * 11) >> 2);
			}
			break;
	}

	return (trxRxNoise < squelch);
}

void trxCheckDigitalSquelch(void)
{
	trx_measure_count++;
	if (trx_measure_count == 25)
	{
		uint8_t squelch;

		trxReadRSSIAndNoise();


		// Don't check for variable squelch, as some people seem to have this set to fully open on their DMR channels.
		/*
		if (currentChannelData->sql!=0)
		{
			squelch =  TRX_SQUELCH_MAX - (((currentChannelData->sql-1)*11)>>2);
		}
		else*/
		{
			squelch = TRX_SQUELCH_MAX - (((nonVolatileSettings.squelchDefaults[trxCurrentBand[TRX_RX_FREQ_BAND]]) * 11) >> 2);
		}

		if (trxRxNoise < squelch)
		{
			if(!digitalSignalReceived)
			{
				digitalSignalReceived = true;
				GPIO_PinWrite(GPIO_LEDgreen, Pin_LEDgreen, 1);
			}
		}
		else
		{
			if (digitalSignalReceived)
			{
				digitalSignalReceived = false;
				GPIO_PinWrite(GPIO_LEDgreen, Pin_LEDgreen, 0);
			}
		}

		trx_measure_count = 0;
	}
}

void trxCheckAnalogSquelch(void)
{
	trx_measure_count++;
	if (trx_measure_count == 25)
	{
		uint8_t squelch;//=45;

		trxReadRSSIAndNoise();

		// check for variable squelch control
		if (currentChannelData->sql != 0)
		{
			squelch = TRX_SQUELCH_MAX - (((currentChannelData->sql - 1) * 11) >> 2);
		}
		else
		{
			squelch = TRX_SQUELCH_MAX - (((nonVolatileSettings.squelchDefaults[trxCurrentBand[TRX_RX_FREQ_BAND]]) * 11) >> 2);
		}

		if (trxRxNoise < squelch)
		{
			if(!analogSignalReceived)
			{
				analogSignalReceived = true;
				displayLightTrigger();
				GPIO_PinWrite(GPIO_LEDgreen, Pin_LEDgreen, 1);
			}
		}
		else
		{
			if(analogSignalReceived)
			{
				analogSignalReceived=false;
				GPIO_PinWrite(GPIO_LEDgreen, Pin_LEDgreen, 0);
			}
		}


		if ((trxRxNoise < squelch) && (((rxCSSactive) && (trxCheckCSSFlag(currentChannelData->rxTone))) || (!rxCSSactive)))
		{
			if (!voicePromptsIsPlaying())
			{
				GPIO_PinWrite(GPIO_RX_audio_mux, Pin_RX_audio_mux, 1);// Set the audio path to AT1846 -> audio amp.
				enableAudioAmp(AUDIO_AMP_MODE_RF);
			}
		}
		else
		{

			if (trxIsTransmittingTone == false)
			{
				trx_css_measure_count++;
				// If using CTCSS or DCS and signal isn't lost, allow some loss of tone / code
				if ((!rxCSSactive) || (trxRxNoise > squelch) || (trx_css_measure_count >= 8))
				{
					disableAudioAmp(AUDIO_AMP_MODE_RF);
					trx_css_measure_count = 0;
				}
			}
		}

    	trx_measure_count=0;
	}
}

void trxSetFrequency(int fRx,int fTx, int dmrMode)
{
	taskENTER_CRITICAL();
	if (currentRxFrequency!=fRx || currentTxFrequency!=fTx)
	{
		trxCurrentBand[TRX_RX_FREQ_BAND] = trxGetBandFromFrequency(fRx);
		trxCurrentBand[TRX_TX_FREQ_BAND] = trxGetBandFromFrequency(fTx);

		calibrationGetPowerForFrequency(fTx, &trxPowerSettings);
		trxSetPowerFromLevel(nonVolatileSettings.txPowerLevel);

		currentRxFrequency = fRx;
		currentTxFrequency = fTx;

		if (dmrMode == DMR_MODE_AUTO)
		{
			// Most DMR radios determine whether to use Active or Passive DMR depending on whether the Tx and Rx freq are the same
			// This prevents split simplex operation, but since no other radio appears to support split freq simplex
			// Its easier to do things the same way as othe radios, and revisit this again in the future if split freq simplex is required.
			if (currentRxFrequency == currentTxFrequency)
			{
				trxDMRMode = DMR_MODE_ACTIVE;
			}
			else
			{
				trxDMRMode = DMR_MODE_PASSIVE;
			}
		}
		else
		{
			trxDMRMode = dmrMode;
		}

		uint32_t f = currentRxFrequency * 0.16f;
		rx_fl_l = (f & 0x000000ff) >> 0;
		rx_fl_h = (f & 0x0000ff00) >> 8;
		rx_fh_l = (f & 0x00ff0000) >> 16;
		rx_fh_h = (f & 0xff000000) >> 24;

		f = currentTxFrequency * 0.16f;
		tx_fl_l = (f & 0x000000ff) >> 0;
		tx_fl_h = (f & 0x0000ff00) >> 8;
		tx_fh_l = (f & 0x00ff0000) >> 16;
		tx_fh_h = (f & 0xff000000) >> 24;

		if (currentMode == RADIO_MODE_DIGITAL)
		{
			terminate_digital();
		}

		if (currentBandWidthIs25kHz)
		{
			// 25 kHz settings
			I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, 0x30, 0x70, 0x06 | squelch); // RX off
		}
		else
		{
			// 12.5 kHz settings
			I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, 0x30, 0x40, 0x06 | squelch); // RX off
		}
		I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, 0x05, 0x87, 0x63); // select 'normal' frequency mode

		I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, 0x29, rx_fh_h, rx_fh_l);
		I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, 0x2a, rx_fl_h, rx_fl_l);
		I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, 0x49, 0x0C, 0x15); // setting SQ open and shut threshold

		if (currentBandWidthIs25kHz)
		{
			// 25 kHz settings
			I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, 0x30, 0x70, 0x26 | squelch); // RX on
		}
		else
		{
			// 12.5 kHz settings
			I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, 0x30, 0x40, 0x26 | squelch); // RX on
		}

		trxUpdateC6000Calibration();
		trxUpdateAT1846SCalibration();

		if (!txPAEnabled)
		{
			if (trxCurrentBand[TRX_RX_FREQ_BAND] == RADIO_BAND_VHF)
			{
				GPIO_PinWrite(GPIO_VHF_RX_amp_power, Pin_VHF_RX_amp_power, 1);
				GPIO_PinWrite(GPIO_UHF_RX_amp_power, Pin_UHF_RX_amp_power, 0);
			}
			else
			{
				GPIO_PinWrite(GPIO_VHF_RX_amp_power, Pin_VHF_RX_amp_power, 0);
				GPIO_PinWrite(GPIO_UHF_RX_amp_power, Pin_UHF_RX_amp_power, 1);
			}
		}
		else
		{
			//SEGGER_RTT_printf(0, "ERROR Cant enable Rx when PA active\n");
		}

		if (currentMode == RADIO_MODE_DIGITAL)
		{
			init_digital();
		}
	}
	taskEXIT_CRITICAL();
}

int trxGetFrequency(void)
{
	if (trxTransmissionEnabled)
	{
		return currentTxFrequency;
	}
	else
	{
		return currentRxFrequency;
	}
}

void trx_setRX(void)
{
//	set_clear_I2C_reg_2byte_with_mask(0x30, 0xFF, 0x1F, 0x00, 0x00);
	if (currentMode == RADIO_MODE_ANALOG)
	{
		trxActivateRx();
	}

}

void trx_setTX(void)
{
	trxTransmissionEnabled = true;

	// RX pre-amp off
	GPIO_PinWrite(GPIO_VHF_RX_amp_power, Pin_VHF_RX_amp_power, 0);
	GPIO_PinWrite(GPIO_UHF_RX_amp_power, Pin_UHF_RX_amp_power, 0);

	//	set_clear_I2C_reg_2byte_with_mask(0x30, 0xFF, 0x1F, 0x00, 0x00);
	if (currentMode == RADIO_MODE_ANALOG)
	{
		trxActivateTx();
	}

}

void trxAT1846RxOff(void)
{
	AT1846SetClearReg2byteWithMask(0x30, 0xFF, 0xDF, 0x00, 0x00);
}

void trxAT1846RxOn(void)
{
	AT1846SetClearReg2byteWithMask(0x30, 0xFF, 0xFF, 0x00, 0x20);
}

void trxActivateRx(void)
{
	//SEGGER_RTT_printf(0, "trx_activateRx\n");
    DAC_SetBufferValue(DAC0, 0U, 0U);// PA drive power to zero

    // Possibly quicker to turn them both off, than to check which on is on and turn that one off
	GPIO_PinWrite(GPIO_VHF_TX_amp_power, Pin_VHF_TX_amp_power, 0);// VHF PA off
	GPIO_PinWrite(GPIO_UHF_TX_amp_power, Pin_UHF_TX_amp_power, 0);// UHF PA off

	txPAEnabled=false;

    if (trxCurrentBand[TRX_RX_FREQ_BAND] == RADIO_BAND_VHF)
	{
		GPIO_PinWrite(GPIO_VHF_RX_amp_power, Pin_VHF_RX_amp_power, 1);// VHF pre-amp on
		GPIO_PinWrite(GPIO_UHF_RX_amp_power, Pin_UHF_RX_amp_power, 0);// UHF pre-amp off
	}
    else
	{
		GPIO_PinWrite(GPIO_VHF_RX_amp_power, Pin_VHF_RX_amp_power, 0);// VHF pre-amp off
		GPIO_PinWrite(GPIO_UHF_RX_amp_power, Pin_UHF_RX_amp_power, 1);// UHF pre-amp on
	}

	if (currentBandWidthIs25kHz)
	{
		I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, 0x30, 0x70, 0x06); 		// 25 kHz settings // RX off
	}
	else
	{
		I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, 0x30, 0x40, 0x06); 		// 12.5 kHz settings // RX off
	}

	I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, 0x29, rx_fh_h, rx_fh_l);
	I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, 0x2a, rx_fl_h, rx_fl_l);

	if (currentBandWidthIs25kHz)
	{
		I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, 0x30, 0x70, 0x26); // 25 kHz settings // RX on
	}
	else
	{
		I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, 0x30, 0x40, 0x26); // 12.5 kHz settings // RX on
	}
}

void trxActivateTx(void)
{
	txPAEnabled = true;
	I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, 0x29, tx_fh_h, tx_fh_l);
	I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, 0x2a, tx_fl_h, tx_fl_l);

	AT1846SetClearReg2byteWithMask(0x30, 0xFF, 0x1F, 0x00, 0x00); // Clear Tx and Rx bits
	if (currentMode == RADIO_MODE_ANALOG)
	{
		AT1846SetClearReg2byteWithMask(0x30, 0xFF, 0x1F, 0x00, 0x40); // analog TX
		trxSelectVoiceChannel(AT1846_VOICE_CHANNEL_MIC);// For 1750 tone burst
		setMicGainFM(nonVolatileSettings.micGainFM);
	}
	else
	{
		AT1846SetClearReg2byteWithMask(0x30, 0xFF, 0x1F, 0x00, 0xC0); // digital TX
	}

	// TX PA on
	if (trxCurrentBand[TRX_TX_FREQ_BAND] == RADIO_BAND_VHF)
	{
		GPIO_PinWrite(GPIO_UHF_TX_amp_power, Pin_UHF_TX_amp_power, 0);// I can't see why this would be needed. Its probably just for safety.
		GPIO_PinWrite(GPIO_VHF_TX_amp_power, Pin_VHF_TX_amp_power, 1);
	}
	else
	{
		GPIO_PinWrite(GPIO_VHF_TX_amp_power, Pin_VHF_TX_amp_power, 0);// I can't see why this would be needed. Its probably just for safety.
		GPIO_PinWrite(GPIO_UHF_TX_amp_power, Pin_UHF_TX_amp_power, 1);
	}
    DAC_SetBufferValue(DAC0, 0U, txPower);	// PA drive to appropriate level
}

void trxSetPowerFromLevel(int powerLevel)
{
// Note. Fraction values for 200Mhz are currently the same as the VHF band, because there isn't any way to set the 1W value on 220Mhz as there are only 2 calibration tables
#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S)

//fractal powers must match last 2 counts of power level
static const float fractionalPowers[12][14] = {	{0.10,0.20,0.25,0.30,0.35,0.39,0.42,0.46,0.50,0.53,0.56,0.72,0.85,0.93},// VHF
												{0.10,0.20,0.25,0.30,0.38,0.42,0.45,0.48,0.52,0.55,0.58,0.73,0.86,0.93},// 220Mhz
												{0.10,0.20,0.26,0.32,0.41,0.45,0.48,0.51,0.55,0.58,0.64,0.77,0.86,0.93}};// UHF

#elif defined(PLATFORM_DM1801)


static const float fractionalPowers[3][7] = {	{0.28,0.37,0.62,0.82,0.60,0.72,0.77},// VHF - THESE VALUE HAVE NOT BEEN CALIBRATED
												{0.28,0.37,0.62,0.82,0.49,0.64,0.73},// 220Mhz - THESE VALUE HAVE NOT BEEN CALIBRATED
												{0.05,0.25,0.51,0.75,0.49,0.64,0.71}};// UHF - THESE VALUE HAVE NOT BEEN CALIBRATED

#elif defined(PLATFORM_RD5R)

static const float fractionalPowers[3][7] = {	{0.37,0.54,0.73,0.87,0.49,0.64,0.73},// VHF
												{0.28,0.37,0.62,0.82,0.49,0.64,0.71},// 220Mhz - THESE VALUE HAVE NOT BEEN CALIBRATED
												{0.05,0.25,0.45,0.85,0.49,0.64,0.71}};// UHF

#endif

#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S)

// must add in new power levels in uiutilities.c
	switch(powerLevel)
	{
		case 0:// 0.1mW //<1mW
		case 1:// 0.3mW
		case 2:// 0.5mW
		case 3:// 1mW
		case 4:// 5mW
		case 5:// 10mW
		case 6:// 25mW
		case 7:// 50mW
		case 8:// 75mW
		case 9:// 100mW
		case 10:// 250mW
		case 11:// 500mW
		case 12:// 750mW
			txPower = trxPowerSettings.lowPower * fractionalPowers[trxCurrentBand[TRX_TX_FREQ_BAND]][powerLevel];
			break;
		case 13:// 1W //Must match case # in Settings.c / powerlevel - this case
			txPower = trxPowerSettings.lowPower;
			break;
		case 14:// 2W
		case 15:// 3W
		case 16:// 4W
			{
				int stepPerWatt = (trxPowerSettings.highPower - trxPowerSettings.lowPower)/( 5 - 1);
				txPower = (((powerLevel - 13) * stepPerWatt) * fractionalPowers[trxCurrentBand[TRX_TX_FREQ_BAND]][powerLevel-1]) + trxPowerSettings.lowPower;
			}
			break;
		case 17:// 5W
			txPower = trxPowerSettings.highPower;
			break;
		case 18:// 5W+
			txPower = 4095;
			break;
			
#else
	switch(powerLevel)
	{
		case 0:// 50mW
		case 1:// 250mW
		case 2:// 500mW
		case 3:// 750mW
			txPower = trxPowerSettings.lowPower * fractionalPowers[trxCurrentBand[TRX_TX_FREQ_BAND]][powerLevel];
			break;
		case 4:// 1W
			txPower = trxPowerSettings.lowPower;
			break;
		case 5:// 2W
		case 6:// 3W
		case 7:// 4W
			{
				int stepPerWatt = (trxPowerSettings.highPower - trxPowerSettings.lowPower)/( 5 - 1);
				txPower = (((powerLevel - 3) * stepPerWatt) * fractionalPowers[trxCurrentBand[TRX_TX_FREQ_BAND]][powerLevel-1]) + trxPowerSettings.lowPower;
			}
			break;
		case 8:// 5W
			txPower = trxPowerSettings.highPower;
			break;
		case 9:// 5W+
			txPower = 4095;
			break;
#endif

		default:
			txPower = trxPowerSettings.lowPower;
			break;
	}

	if (txPower > 4095)
	{
		txPower = 4095;
	}
}

uint16_t trxGetPower(void)
{
	return txPower;
}

void trxCalcBandAndFrequencyOffset(CalibrationBand_t *calibrationBand, uint32_t *freq_offset)
{
// NOTE. For crossband duplex DMR, the calibration potentially needs to be changed every time the Tx/Rx is switched over on each 30ms cycle
// But at the moment this is an unnecessary complication and I'll just use the Rx frequency to get the calibration offsets

	if (trxCurrentBand[TRX_RX_FREQ_BAND] == RADIO_BAND_UHF)
	{
		*calibrationBand = CalibrationBandUHF;
		*freq_offset = (currentTxFrequency - 40000000)/1000000;
	}
	else
	{
		*calibrationBand = CalibrationBandVHF;
		*freq_offset = (currentTxFrequency - 13250000)/500000;
	}

	// Limit VHF freq calculation exceeds the max lookup table index (of 7)
	if (*freq_offset > 7)
	{
		*freq_offset = 7;
	}
}

void trxUpdateC6000Calibration(void)
{
	uint32_t freq_offset;
	CalibrationBand_t calBand;
	CalibrationDataResult_t calRes;

	if (nonVolatileSettings.useCalibration == false)
	{
		return;
	}

	trxCalcBandAndFrequencyOffset(&calBand, &freq_offset);

	SPI0WritePageRegByte(0x04, 0x00, 0x3F); // Reset HR-C6000 state

	calibrationGetSectionData(calBand, CalibrationSection_DACDATA_SHIFT, &calRes);
	SPI0WritePageRegByte(0x04, 0x37, calRes.value); // DACDATA shift (LIN_VOL)

	calibrationGetSectionData(calBand, CalibrationSection_Q_MOD2_OFFSET, &calRes);
	SPI0WritePageRegByte(0x04, 0x04, calRes.value); // MOD2 offset

	calRes.offset = freq_offset;
	calibrationGetSectionData(calBand, CalibrationSection_PHASE_REDUCE, &calRes);
	SPI0WritePageRegByte(0x04, 0x46, calRes.value); // phase reduce

	calibrationGetSectionData(calBand, CalibrationSection_TWOPOINT_MOD, &calRes);
	uint16_t refOscOffset = calRes.value; //(highByte<<8)+lowByte;

/*
 * Hack to bring the RD-5R to the correct frequency
 * I don't know why the RD-5R calibration seems to result in this firmware being about 1.4khz too high on VHF
 * This offset value seems to be consistent on various RD-5R radios, as reported by operators on the OpenGD77 forum.
 *
 * However it now appears that not all RD-5R radios need this, or possibly the RD-5R needs it but the DM-5R does not
 * So this line has been commented out until the situation becomes clearer
 *
 * refOscOffset -= 38;
 *
 */
	if (refOscOffset > 1023)
	{
		refOscOffset = 1023;
	}
	SPI0WritePageRegByte(0x04, 0x48, (refOscOffset >> 8) & 0x03); // bit 0 to 1 = upper 2 bits of 10-bit twopoint mod
	SPI0WritePageRegByte(0x04, 0x47, (refOscOffset & 0xFF)); // bit 0 to 7 = lower 8 bits of 10-bit twopoint mod
}

void I2C_AT1846_set_register_with_mask(uint8_t reg, uint16_t mask, uint16_t value, uint8_t shift)
{
	AT1846SetClearReg2byteWithMask(reg, (mask & 0xff00) >> 8, (mask & 0x00ff) >> 0, ((value << shift) & 0xff00) >> 8, ((value << shift) & 0x00ff) >> 0);
}

void trxUpdateAT1846SCalibration(void)
{
	uint32_t freq_offset = 0x00000000;
	CalibrationBand_t calBand;
	CalibrationDataResult_t calRes;

	if (nonVolatileSettings.useCalibration == false)
	{
		return;
	}

	trxCalcBandAndFrequencyOffset(&calBand, &freq_offset);

	uint8_t val_pga_gain;
	uint8_t gain_tx;
	uint8_t padrv_ibit;

	uint16_t xmitter_dev;

	uint8_t dac_vgain_analog;
	uint8_t volume_analog;

	uint16_t noise1_th;
	uint16_t noise2_th;
	uint16_t rssi3_th;

	uint16_t squelch_th;

	calibrationGetSectionData(calBand, CalibrationSection_PGA_GAIN, &calRes);
	val_pga_gain = calRes.value;

	calibrationGetSectionData(calBand, CalibrationSection_VOICE_GAIN_TX, &calRes);
	voice_gain_tx = calRes.value;

	calibrationGetSectionData(calBand, CalibrationSection_GAIN_TX, &calRes);
	gain_tx = calRes.value;

	calibrationGetSectionData(calBand, CalibrationSection_PADRV_IBIT, &calRes);
	padrv_ibit = calRes.value;


	// 25 or 12.5 kHz settings
	calibrationGetSectionData(calBand,
			(currentBandWidthIs25kHz ? CalibrationSection_XMITTER_DEV_WIDEBAND : CalibrationSection_XMITTER_DEV_NARROWBAND), &calRes);
	xmitter_dev = calRes.value;

	if (currentMode == RADIO_MODE_ANALOG)
	{
		calibrationGetSectionData(calBand, CalibrationSection_DAC_VGAIN_ANALOG, &calRes);
		dac_vgain_analog = calRes.value;

		calibrationGetSectionData(calBand, CalibrationSection_VOLUME_ANALOG, &calRes);
		volume_analog = calRes.value;
	}
	else
	{
		dac_vgain_analog = 0x0C;
		volume_analog = 0x0C;
	}

	calibrationGetSectionData(calBand,
			(currentBandWidthIs25kHz ? CalibrationSection_NOISE1_TH_WIDEBAND : CalibrationSection_NOISE1_TH_NARROWBAND), &calRes);
	noise1_th = calRes.value;

	calibrationGetSectionData(calBand,
			(currentBandWidthIs25kHz ? CalibrationSection_NOISE2_TH_WIDEBAND : CalibrationSection_NOISE2_TH_NARROWBAND), &calRes);
	noise2_th = calRes.value;

	calibrationGetSectionData(calBand,
			(currentBandWidthIs25kHz ? CalibrationSection_RSSI3_TH_WIDEBAND : CalibrationSection_RSSI3_TH_NARROWBAND), &calRes);
	rssi3_th = calRes.value;

	calRes.mod = (currentBandWidthIs25kHz ? 0 : 3);
	calibrationGetSectionData(calBand, CalibrationSection_SQUELCH_TH, &calRes);
	squelch_th = calRes.value;

	I2C_AT1846_set_register_with_mask(0x0A, 0xF83F, val_pga_gain, 6);
	I2C_AT1846_set_register_with_mask(0x41, 0xFF80, voice_gain_tx, 0);
	I2C_AT1846_set_register_with_mask(0x44, 0xF0FF, gain_tx, 8);

	I2C_AT1846_set_register_with_mask(0x59, 0x003f, xmitter_dev, 6);
	I2C_AT1846_set_register_with_mask(0x44, 0xFF0F, dac_vgain_analog, 4);
	I2C_AT1846_set_register_with_mask(0x44, 0xFFF0, volume_analog, 0);

	I2C_AT1846_set_register_with_mask(0x48, 0x0000, noise1_th, 0);
	I2C_AT1846_set_register_with_mask(0x60, 0x0000, noise2_th, 0);
	I2C_AT1846_set_register_with_mask(0x3f, 0x0000, rssi3_th, 0);

	I2C_AT1846_set_register_with_mask(0x0A, 0x87FF, padrv_ibit, 11);

	I2C_AT1846_set_register_with_mask(0x49, 0x0000, squelch_th, 0);
}

void trxSetDMRColourCode(int colourCode)
{
	SPI0WritePageRegByte(0x04, 0x1F, (colourCode << 4)); // DMR Colour code in upper 4 bits.
	currentCC = colourCode;
}

int trxGetDMRColourCode(void)
{
	return currentCC;
}

int trxGetDMRTimeSlot(void)
{
	return trxCurrentDMRTimeSlot;
	//return ((currentChannelData->flag2 & 0x40)!=0);
}

void trxSetDMRTimeSlot(int timeslot)
{
	trxCurrentDMRTimeSlot = timeslot;
}

void trxUpdateTsForCurrentChannelWithSpecifiedContact(struct_codeplugContact_t *contactData)
{
	bool hasManualTsOverride = false;

	// nonVolatileSettings.tsManualOverride stores separate TS overrides for VFO A, VFO B and Channel mode
	// Use tsIsOverriden(), tsGetOverride(), tsSetOverride() to access the overridden
	if (nonVolatileSettings.initialMenuNumber == UI_CHANNEL_MODE)
	{
		if (tsIsOverridden(CHANNEL_CHANNEL))
		{
			hasManualTsOverride = true;
		}
	}
	else
	{
		if (tsIsOverridden(CHANNEL_VFO_A) || tsIsOverridden(CHANNEL_VFO_B))
		{
			hasManualTsOverride = true;
		}
	}

	if (!hasManualTsOverride)
	{
		if ((contactData->reserve1 & 0x01) == 0x00)
		{
			if ((contactData->reserve1 & 0x02) != 0)
			{
				trxCurrentDMRTimeSlot = 1;
			}
			else
			{
				trxCurrentDMRTimeSlot = 0;
			}
		}
		else
		{
			trxCurrentDMRTimeSlot = ((currentChannelData->flag2 & 0x40) != 0);
		}
	}
}

void trxSetTxCSS(uint16_t tone)
{
	taskENTER_CRITICAL();
	if (tone == CODEPLUG_CSS_NONE)
	{
		// tone value of 0xffff in the codeplug seem to be a flag that no tone has been selected
		// Zero the CTCSS1 Register
		I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, 0x4a, 0x00,0x00);
		// disable the transmit CTCSS
		AT1846SetClearReg2byteWithMask(0x4e,0xF9,0xFF,0x00,0x00);
	}
	else if (codeplugChannelToneIsCTCSS(tone))
	{
		// value that is stored is 100 time the tone freq but its stored in the codeplug as freq times 10
		tone = tone * 10;
		I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT,	0x4a, (tone >> 8) & 0xff, (tone & 0xff));
		// init cdcss_code
		I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT,	0x4b, 0x00, 0x00);
		// init cdcss_code
		I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT,	0x4c, 0x0A, 0xE3);
		//enable the transmit CTCSS
		AT1846SetClearReg2byteWithMask(0x4e, 0xF9, 0xFF, 0x06, 0x00);
	}
	else if (codeplugChannelToneIsDCS(tone))
	{
		// Set the CTCSS1 Register to 134.4Hz (DCS data rate)
		I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT,	0x4a, (TRX_DCS_TONE >> 8) & 0xff, TRX_DCS_TONE & 0xff);
		// Zero the CTCSS2 Register
		I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, 0x4d, 0x00, 0x00);
		// The AT1846S wants the Golay{23,12} encoding of the DCS code, rather than just the code itself.
		uint32_t encoded = trxDCSEncode(tone & ~CODEPLUG_DCS_FLAGS_MASK);
		uint8_t tx_flags_high = 0x04;
		if (tone & CODEPLUG_DCS_INVERTED_MASK)
		{
			tx_flags_high |= 0x01;
		}
		I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT,	0x4b, 0x00, (encoded >> 16) & 0xff);           // init cdcss_code
		I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT,	0x4c, (encoded >> 8) & 0xff, encoded & 0xff);  // init cdcss_code
		AT1846SetClearReg2byteWithMask(0x3a, 0xFF, 0xE0, 0x00, 0x06); // enable receive DCS
		AT1846SetClearReg2byteWithMask(0x4e, 0x38, 0x3F, tx_flags_high, 0x00); // enable transmit DCS
	}
	taskEXIT_CRITICAL();
}

void trxSetRxCSS(uint16_t tone)
{
	taskENTER_CRITICAL();
	if (tone == CODEPLUG_CSS_NONE)
	{
		// tone value of 0xffff in the codeplug seem to be a flag that no tone has been selected
		// Zero the CTCSS2 Register
		I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, 0x4d, 0x00, 0x00);
		rxCSSactive = false;
	}
	else if (codeplugChannelToneIsCTCSS(tone))
	{
		int threshold = (2500 - tone) / 100;  // adjust threshold value to match tone frequency.
		if (tone > 2400) threshold=1;
		// value that is stored is 100 time the tone freq but its stored in the codeplug as freq times 10
		tone = tone * 10;
		I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, 0x4d, (tone >> 8) & 0xff, (tone & 0xff));
		//set the detection thresholds
		I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, 0x5b, (threshold & 0xFF), (threshold & 0xFF));
		//set detection to CTCSS2
		AT1846SetClearReg2byteWithMask(0x3a, 0xFF, 0xE0, 0x00, 0x08);
		rxCSSactive = (nonVolatileSettings.analogFilterLevel != ANALOG_FILTER_NONE);
	}
	else if (codeplugChannelToneIsDCS(tone))
	{
		// Set the CTCSS1 Register to 134.4Hz (DCS data rate)
		I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT,	0x4a, (TRX_DCS_TONE >> 8) & 0xff, TRX_DCS_TONE & 0xff);
		// Zero the CTCSS2 Register
		I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, 0x4d, 0x00, 0x00);
		// The AT1846S wants the Golay{23,12} encoding of the DCS code, rather than just the code itself.
		uint32_t encoded = trxDCSEncode(tone & ~CODEPLUG_DCS_FLAGS_MASK);
		I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, 0x4b, 0x00, (encoded >> 16) & 0xff);           // init cdcss_code
		I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, 0x4c, (encoded >> 8) & 0xff, encoded & 0xff);  // init cdcss_code
		AT1846SetClearReg2byteWithMask(0x3a, 0xFF, 0xE0, 0x00, 0x06); // enable receive DCS
		// The cdcss_sel bits have to be set for DCS receive to work
		AT1846SetClearReg2byteWithMask(0x4e, 0x38, 0x3F, 0x04, 0x00); // enable transmit DCS
		//set_clear_I2C_reg_2byte_with_mask(0x4e, 0xF9, 0xFF, 0x04, 0x00); // enable transmit DCS
		rxCSSactive = (nonVolatileSettings.analogFilterLevel != ANALOG_FILTER_NONE);
	}
	taskEXIT_CRITICAL();
}

bool trxCheckCSSFlag(uint16_t tone)
{
	uint8_t FlagsH;
	uint8_t FlagsL;

	taskENTER_CRITICAL();
	I2CReadReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, 0x1c, &FlagsH, &FlagsL);
	taskEXIT_CRITICAL();
	// Could instead check both flags in one go?
	if (codeplugChannelToneIsCTCSS(tone))
	{
		return (FlagsH & 0x01);
	}
	else if (codeplugChannelToneIsDCS(tone))
	{
		if (FlagsL & 0xC4)
		{
			return true;
		}
	}
	return false;
}


void trxUpdateDeviation(int channel)
{
	CalibrationBand_t calBand = ((trxCurrentBand[TRX_RX_FREQ_BAND] == RADIO_BAND_UHF) ? CalibrationBandUHF : CalibrationBandVHF);
	CalibrationDataResult_t calRes;
	uint8_t deviation;

	if (nonVolatileSettings.useCalibration == false)
	{
		return;
	}

	taskENTER_CRITICAL();
	switch (channel)
	{
		case AT1846_VOICE_CHANNEL_TONE1:
		case AT1846_VOICE_CHANNEL_TONE2:
			calRes.offset = CAL_DEV_TONE;
			calibrationGetSectionData(calBand, CalibrationSection_DEV_TONE, &calRes);
			deviation = (calRes.value & 0x7f);
			//I2C_AT1846_set_register_with_mask(0x59, 0x003f, deviation, 6); // THIS IS THE WRONG REGISTER TO CONTROL 'TONE' DEV. SEE THE LINE BELOW INSTEAD , reg  Tone deviation value
			I2C_AT1846_set_register_with_mask(0x41, 0xFF80, deviation, 0);// Tone deviation value
			break;

		case AT1846_VOICE_CHANNEL_DTMF:
			calRes.offset = CAL_DEV_DTMF;
			calibrationGetSectionData(calBand, CalibrationSection_DEV_TONE, &calRes);
			deviation = (calRes.value & 0x7f);
			//I2C_AT1846_set_register_with_mask(0x59, 0x003f, deviation, 6); // THIS IS THE WRONG REGISTER TO CONTROL DTMF DEV. SEE THE LINE BELOW INSTEAD , reg  Tone deviation value
			I2C_AT1846_set_register_with_mask(0x41, 0xFF80, deviation, 0);//  Tone deviation value
			break;
	}
	taskEXIT_CRITICAL();
}

uint8_t trxGetCalibrationVoiceGainTx(void)
{
	return voice_gain_tx;
}

void trxSelectVoiceChannel(uint8_t channel) {
	uint8_t valh;
	uint8_t vall;

	taskENTER_CRITICAL();
	switch (channel)
	{
	case AT1846_VOICE_CHANNEL_TONE1:
	case AT1846_VOICE_CHANNEL_TONE2:
	case AT1846_VOICE_CHANNEL_DTMF:
		AT1846SetClearReg2byteWithMask(0x79, 0xff, 0xff, 0xc0, 0x00); // Select single tone
		AT1846SetClearReg2byteWithMask(0x57, 0xff, 0xfe, 0x00, 0x01); // Audio feedback on

		I2CReadReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, 0x41, &valh, &trxSaveVoiceGainTx);
		trxSaveVoiceGainTx &= 0x7f;

		I2CReadReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, 0x59, &valh, &vall);
		trxSaveDeviation = (vall + (valh<<8)) >> 6;

		trxUpdateDeviation(channel);

		//AT1846SetClearReg2byteWithMask(0x41, 0xff, 0x80, 0x00, 0x05);// Commented out because the deviation set in trxUpdateDeviation uses reg 0x41
		break;
	default:
		AT1846SetClearReg2byteWithMask(0x57, 0xff, 0xfe, 0x00, 0x00); // Audio feedback off
		if (trxSaveVoiceGainTx != 0xff)
		{
			I2C_AT1846_set_register_with_mask(0x41, 0xFF80, trxSaveVoiceGainTx, 0);
			trxSaveVoiceGainTx = 0xff;
		}
		if (trxSaveDeviation != 0xFF)
		{
			I2C_AT1846_set_register_with_mask(0x59, 0x003f, trxSaveDeviation, 6);
			trxSaveDeviation = 0xFF;
		}
		break;
	}
	AT1846SetClearReg2byteWithMask(0x3a, 0x8f, 0xff, channel, 0x00);
	taskEXIT_CRITICAL();
}

void trxSetTone1(int toneFreq)
{

	toneFreq = toneFreq * 10;
	taskENTER_CRITICAL();
	I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, 0x35, (toneFreq >> 8) & 0xff, (toneFreq & 0xff));   // tone1_freq
	taskEXIT_CRITICAL();
}

void trxSetTone2(int toneFreq)
{
	toneFreq = toneFreq * 10;
	taskENTER_CRITICAL();
	I2CWriteReg2byte(AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT, 0x36, (toneFreq >> 8) & 0xff, (toneFreq & 0xff));   // tone2_freq
	taskEXIT_CRITICAL();
}

void trxSetDTMF(int code)
{
	if (code < 16)
	{
		trxSetTone1(trxDTMFfreq1[code]);
		trxSetTone2(trxDTMFfreq2[code]);
	}
}

uint32_t trxDCSEncode(uint16_t code)
{
	return (TRX_DCSECCBits[code] << 12) | 04000 | code;
}

void setMicGainFM(uint8_t gain)
{
	uint8_t voice_gain_tx = trxGetCalibrationVoiceGainTx();

	// Apply extra gain 17 (the calibration default value, not the datasheet one)
	if (gain > 17)
	{
		//voice_gain_tx += (((gain - 16) * 3) >> 1); // Get some Larsen starting at gain:11
		voice_gain_tx += (gain - 16); // Seems to be enough
	}

	I2C_AT1846_set_register_with_mask(0x0A, 0xF83F, gain, 6);
	I2C_AT1846_set_register_with_mask(0x41, 0xFF80, voice_gain_tx, 0);
}

void enableTransmission(void)
{
	GPIO_PinWrite(GPIO_LEDgreen, Pin_LEDgreen, 0);
	GPIO_PinWrite(GPIO_LEDred, Pin_LEDred, 1);

	txstopdelay = 0;
	trx_setTX();
}

void disableTransmission(void)
{
	GPIO_PinWrite(GPIO_LEDred, Pin_LEDred, 0);
	// Need to wrap this in Task Critical to avoid bus contention on the I2C bus.
	taskENTER_CRITICAL();
	trxActivateRx();
	taskEXIT_CRITICAL();
	//trxSetFrequency(freq_rx,freq_tx);
}

