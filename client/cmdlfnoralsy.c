//-----------------------------------------------------------------------------
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// Low frequency Presco tag commands
//-----------------------------------------------------------------------------

#include "cmdlfnoralsy.h"

static int CmdHelp(const char *Cmd);

int usage_lf_noralsy_clone(void){
	PrintAndLog("clone a Noralsy tag to a T55x7 tag.");
	PrintAndLog("Usage: lf noralsy clone [h] <card ID> <Q5>");
	PrintAndLog("Options:");
	PrintAndLog("      h          : This help");
	PrintAndLog("      <card ID>  : Noralsy card ID");
	PrintAndLog("      <Q5>       : specify write to Q5 (t5555 instead of t55x7)");
	PrintAndLog("");
	PrintAndLog("Sample: lf noralsy clone 112233");
	return 0;
}

int usage_lf_noralsy_sim(void) {
	PrintAndLog("Enables simulation of Noralsy card with specified card number.");
	PrintAndLog("Simulation runs until the button is pressed or another USB command is issued.");
	PrintAndLog("");
	PrintAndLog("Usage:  lf noralsy sim [h] <card ID>");
	PrintAndLog("Options:");
	PrintAndLog("      h          : This help");
	PrintAndLog("      <card ID>  : Noralsy card ID");
	PrintAndLog("");
	PrintAndLog("Sample: lf noralsy sim 112233");
	return 0;
}

static uint8_t noralsy_chksum( uint8_t* bits, uint8_t len) {
    uint8_t sum = 0;
    for (uint8_t i = 0; i < len; i += 4)
        sum ^= bytebits_to_byte(bits+i, 4);
    return sum & 0x0F ;
}
int getnoralsyBits(uint32_t id, uint8_t *bits) {	
	//preamp
	num_to_bytebits(0xBB0214FF, 32, bits);  // --> Have seen 0xBB0214FF / 0xBB0314FF  UNKNOWN

	//convert ID into BCD-format
	id = DEC2BCD(id);

	uint16_t sub1 = (id & 0xFFF0000) >> 16;
	uint8_t sub2 = (id & 0x000FF00) >> 8;
	uint8_t sub3 = (id & 0x00000FF);
	
	num_to_bytebits(sub1, 12, bits+32);
	num_to_bytebits(0x980, 12, bits+44);   // --> UNKNOWN part
	num_to_bytebits(sub2, 8, bits+56);
	num_to_bytebits(sub3, 8, bits+64);

	//chksum byte
	uint8_t chksum = noralsy_chksum(bits+32, 40);
	num_to_bytebits(chksum, 4, bits+72);
	chksum = noralsy_chksum(bits, 76);
	num_to_bytebits(chksum, 4, bits+76);
	return 1;
}

//see ASKDemod for what args are accepted
int CmdNoralsyDemod(const char *Cmd) {

	//ASK / Manchester
	bool st = true;
	if (!ASKDemod_ext("32 0 0", FALSE, FALSE, 1, &st)) {
		if (g_debugMode) PrintAndLog("DEBUG: Error - Noralsy: ASK/Manchester Demod failed");
		return 0;
	}
	size_t size = DemodBufferLen;
	int ans = NoralsyDemod_AM(DemodBuffer, &size);
	if (ans < 0){
		if (g_debugMode){
			if (ans == -1)
				PrintAndLog("DEBUG: Error - Noralsy: too few bits found");
			else if (ans == -2)
				PrintAndLog("DEBUG: Error - Noralsy: preamble not found");
			else if (ans == -3)
				PrintAndLog("DEBUG: Error - Noralsy: Size not correct: %d", size);
			else
				PrintAndLog("DEBUG: Error - Noralsy: ans: %d", ans);
		}
		return 0;
	}
	setDemodBuf(DemodBuffer, 96, ans);
	
	//got a good demod
	uint32_t raw1 = bytebits_to_byte(DemodBuffer, 32);
	uint32_t raw2 = bytebits_to_byte(DemodBuffer+32, 32);
	uint32_t raw3 = bytebits_to_byte(DemodBuffer+64, 32);

	uint32_t cardid = ((raw2 & 0xFFF00000) >> 20) << 16;
	cardid |= (raw2 & 0xFF) << 8;
	cardid |= ((raw3 & 0xFF000000) >> 24);
	cardid = BCD2DEC(cardid);
	
	// calc checksums
	uint8_t calc1 = noralsy_chksum(DemodBuffer+32, 40);
	uint8_t calc2 = noralsy_chksum(DemodBuffer, 76);
	uint8_t chk1 = 0, chk2 = 0;
	chk1 = bytebits_to_byte(DemodBuffer+72, 4);
	chk2 = bytebits_to_byte(DemodBuffer+76, 4);
	// test checksums
	if ( chk1 != calc1 ) { 
		printf("checksum 1 failed %x - %x\n", chk1, calc1);
		return 0;
	}
	if ( chk2 != calc2 ) {
		printf("checksum 2 failed %x - %x\n", chk2, calc2);	
		return 0;
	}
	
	PrintAndLog("Noralsy Tag Found: Card ID %u,  Raw: %08X%08X%08X", cardid,  raw1 ,raw2, raw3);
	return 1;
}

int CmdNoralsyRead(const char *Cmd) {
	CmdLFRead("s");
	getSamples("20000",TRUE);
	return CmdNoralsyDemod(Cmd);
}

int CmdNoralsyClone(const char *Cmd) {

	uint32_t id = 0;
	uint32_t blocks[4] = {T55x7_MODULATION_MANCHESTER | T55x7_BITRATE_RF_32 | T55x7_ST_TERMINATOR |3<<T55x7_MAXBLOCK_SHIFT, 0, 0};

	uint8_t bits[96];
	uint8_t *bs = bits;
	memset(bs, 0, sizeof(bits));
	
	char cmdp = param_getchar(Cmd, 0);
	if (strlen(Cmd) == 0 || cmdp == 'h' || cmdp == 'H') return usage_lf_noralsy_clone();

	id = param_get32ex(Cmd, 0, 0, 10);
	
	//Q5
	if (param_getchar(Cmd, 1) == 'Q' || param_getchar(Cmd, 1) == 'q') {
		//t5555 (Q5) BITRATE = (RF-2)/2 (iceman)
		blocks[0] = T5555_MODULATION_MANCHESTER | 32<<T5555_BITRATE_SHIFT | T5555_ST_TERMINATOR | 3<<T5555_MAXBLOCK_SHIFT;
	}
	
	 if ( !getnoralsyBits(id, bs)) {
		PrintAndLog("Error with tag bitstream generation.");
		return 1;
	}	
	
	// 
	blocks[1] = bytebits_to_byte(bs,32);
	blocks[2] = bytebits_to_byte(bs+32,32);
	blocks[3] = bytebits_to_byte(bs+64,32);

	PrintAndLog("Preparing to clone Noralsy to T55x7 with CardId: %u", id);
	PrintAndLog("Blk | Data ");
	PrintAndLog("----+------------");
	PrintAndLog(" 00 | 0x%08x", blocks[0]);
	PrintAndLog(" 01 | 0x%08x", blocks[1]);
	PrintAndLog(" 02 | 0x%08x", blocks[2]);
	PrintAndLog(" 03 | 0x%08x", blocks[3]);
	
	UsbCommand resp;
	UsbCommand c = {CMD_T55XX_WRITE_BLOCK, {0,0,0}};

	for (int i = 3; i >= 0; --i) {
		c.arg[0] = blocks[i];
		c.arg[1] = i;
		clearCommandBuffer();
		SendCommand(&c);
		if (!WaitForResponseTimeout(CMD_ACK, &resp, 1000)){
			PrintAndLog("Error occurred, device did not respond during write operation.");
			return -1;
		}
	}
    return 0;
}

int CmdNoralsySim(const char *Cmd) {

	uint8_t bits[96];
	uint8_t *bs = bits;
	memset(bs, 0, sizeof(bits));
	
	uint32_t id = 0;
	char cmdp = param_getchar(Cmd, 0);
	if (strlen(Cmd) == 0 || cmdp == 'h' || cmdp == 'H') return usage_lf_noralsy_sim();

	id = param_get32ex(Cmd, 0, 0, 10);

	uint8_t clk = 32, encoding = 1, separator = 1, invert = 0;
	uint16_t arg1, arg2;
	size_t size = 96;
	arg1 = clk << 8 | encoding;
	arg2 = invert << 8 | separator;
	
	 if ( !getnoralsyBits(id, bs)) {
		PrintAndLog("Error with tag bitstream generation.");
		return 1;
	}	
	
	PrintAndLog("Simulating Noralsy - CardId: %u", id);

	UsbCommand c = {CMD_ASK_SIM_TAG, {arg1, arg2, size}};
	memcpy(c.d.asBytes, bs, size);
	clearCommandBuffer();
	SendCommand(&c);
	return 0;
}

static command_t CommandTable[] = {
    {"help",	CmdHelp,		1, "This help"},
	{"read",	CmdNoralsyRead,	0, "Attempt to read and extract tag data"},
	{"clone",	CmdNoralsyClone,0, "clone Noralsy tag"},
	{"sim",		CmdNoralsySim,	0, "simulate Noralsy tag"},
    {NULL, NULL, 0, NULL}
};

int CmdLFNoralsy(const char *Cmd) {
	clearCommandBuffer();
    CmdsParse(CommandTable, Cmd);
    return 0;
}

int CmdHelp(const char *Cmd) {
    CmdsHelp(CommandTable);
    return 0;
}
