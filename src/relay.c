/*
 * relay.c:
 *	Command-line interface to the Raspberry
 *	Pi's 4-Relay board.
 *	Copyright (c) 2016-2020 Sequent Microsystem
 *	<http://www.sequentmicrosystem.com>
 ***********************************************************************
 *	Author: Alexandru Burcea
 ***********************************************************************
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "relay.h"
#include "comm.h"
#include "thread.h"

#define VERSION_BASE	(int)1
#define VERSION_MAJOR	(int)0
#define VERSION_MINOR	(int)0

#define UNUSED(X) (void)X      /* To avoid gcc/g++ warnings */
#define CMD_ARRAY_SIZE	7

const u8 relayMaskRemap[RELAY_CH_NR_MAX] =
{
	0x80,
	0x40,
	0x20,
	0x10};
const int relayChRemap[RELAY_CH_NR_MAX] =
{
	7,
	6,
	5,
	4};

const u8 inMaskRemap[IN_CH_NR_MAX] =
{
	0x08,
	0x04,
	0x02,
	0x01};

const int inChRemap[IN_CH_NR_MAX] =
{
	3,
	2,
	1,
	0};

int relayChSet(int dev, u8 channel, OutStateEnumType state);
int relayChGet(int dev, u8 channel, OutStateEnumType* state);
u8 relayToIO(u8 relay);
u8 IOToRelay(u8 io);
u8 IOToIn(u8 io);

static void doHelp(int argc, char *argv[]);
const CliCmdType CMD_HELP =
	{
		"-h",
		1,
		&doHelp,
		"\t-h          Display the list of command options or one command option details\n",
		"\tUsage:      4relplus -h    Display command options list\n",
		"\tUsage:      4relplus -h <param>   Display help for <param> command option\n",
		"\tExample:    4relplus -h write    Display help for \"write\" command option\n"};

static void doVersion(int argc, char *argv[]);
const CliCmdType CMD_VERSION =
{
	"-v",
	1,
	&doVersion,
	"\t-v              Display the version number\n",
	"\tUsage:          4relplus -v\n",
	"",
	"\tExample:        4relplus -v  Display the version number\n"};

static void doWarranty(int argc, char* argv[]);
const CliCmdType CMD_WAR =
{
	"-warranty",
	1,
	&doWarranty,
	"\t-warranty       Display the warranty\n",
	"\tUsage:          4relplus -warranty\n",
	"",
	"\tExample:        4relplus -warranty  Display the warranty text\n"};

static void doList(int argc, char *argv[]);
const CliCmdType CMD_LIST =
	{
		"-list",
		1,
		&doList,
		"\t-list:       List all 4relplus boards connected,\n\treturn       nr of boards and stack level for every board\n",
		"\tUsage:       4relplus -list\n",
		"",
		"\tExample:     4relplus -list display: 1,0 \n"};

static void doRelayWrite(int argc, char *argv[]);
const CliCmdType CMD_WRITE =
{
	"write",
	2,
	&doRelayWrite,
	"\twrite:       Set relays On/Off\n",
	"\tUsage:       4relplus <id> write <channel> <on/off>\n",
	"\tUsage:       4relplus <id> write <value>\n",
	"\tExample:     4relplus 0 write 2 On; Set Relay #2 on Board #0 On\n"};

static void doRelayRead(int argc, char *argv[]);
const CliCmdType CMD_READ =
{
	"read",
	2,
	&doRelayRead,
	"\tread:        Read relays status\n",
	"\tUsage:       4relplus <id> read <channel>\n",
	"\tUsage:       4relplus <id> read\n",
	"\tExample:     4relplus 0 read 2; Read Status of Relay #2 on Board #0\n"};

static void doInRead(int argc, char *argv[]);
const CliCmdType CMD_IN_READ =
{
	"inread",
	2,
	&doInRead,
	"\tinread:        Read inputs status\n",
	"\tUsage:       4relplus <id> inread <channel>\n",
	"\tUsage:       4relplus <id> inread\n",
	"\tExample:     4relplus 0 inread 2; Read Status of Input #2 on Board #0\n"};


static void doTest(int argc, char* argv[]);
const CliCmdType CMD_TEST =
{
	"test",
	2,
	&doTest,
	"\ttest:        Turn ON and OFF the relays until press a key\n",
	"",
	"\tUsage:       4relplus <id> test\n",
	"\tExample:     4relplus 0 test\n"};

CliCmdType gCmdArray[CMD_ARRAY_SIZE];

char *usage = "Usage:	 4relplus -h <command>\n"
	"         4relplus -v\n"
	"         4relplus -warranty\n"
	"         4relplus -list\n"
	"         4relplus <id> write <channel> <on/off>\n"
	"         4relplus <id> write <value>\n"
	"         4relplus <id> read <channel>\n"
	"         4relplus <id> read\n"
	"         4relplus <id> test\n"
	"Where: <id> = Board level id = 0..7\n"
	"Type 4relplus -h <command> for more help"; // No trailing newline needed here.

char *warranty =
	"	       Copyright (c) 2016-2020 Sequent Microsystems\n"
		"                                                             \n"
		"		This program is free software; you can redistribute it and/or modify\n"
		"		it under the terms of the GNU Leser General Public License as published\n"
		"		by the Free Software Foundation, either version 3 of the License, or\n"
		"		(at your option) any later version.\n"
		"                                    \n"
		"		This program is distributed in the hope that it will be useful,\n"
		"		but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
		"		MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
		"		GNU Lesser General Public License for more details.\n"
		"			\n"
		"		You should have received a copy of the GNU Lesser General Public License\n"
		"		along with this program. If not, see <http://www.gnu.org/licenses/>.";
u8 relayToIO(u8 relay)
{
	u8 i;
	u8 val = 0;
	for (i = 0; i < RELAY_CH_NR_MAX; i++)
	{
		if ( (relay & (1 << i)) != 0)
			val += relayMaskRemap[i];
	}
	return val;
}

u8 IOToRelay(u8 io)
{
	u8 i;
	u8 val = 0;
	for (i = 0; i < RELAY_CH_NR_MAX; i++)
	{
		if ( (io & relayMaskRemap[i]) != 0)
		{
			val += 1 << i;
		}
	}
	return val;
}

u8 IOToIn(u8 io)
{
	u8 i;
	u8 val = 0;
	for (i = 0; i < IN_CH_NR_MAX; i++)
	{
		if ( (io & inMaskRemap[i]) == 0)
		{
			val += 1 << i;
		}
	}
	return val;
}

int relayChSet(int dev, u8 channel, OutStateEnumType state)
{
	int resp;
	u8 buff[2];

	if ( (channel < CHANNEL_NR_MIN) || (channel > RELAY_CH_NR_MAX))
	{
		printf("Invalid relay nr!\n");
		return ERROR;
	}
	if (FAIL == i2cMem8Read(dev, RELAY8_INPORT_REG_ADD, buff, 1))
	{
		return FAIL;
	}

	switch (state)
	{
	case OFF:
		buff[0] &= ~ (1 << relayChRemap[channel - 1]);
		resp = i2cMem8Write(dev, RELAY8_OUTPORT_REG_ADD, buff, 1);
		break;
	case ON:
		buff[0] |= 1 << relayChRemap[channel - 1];
		resp = i2cMem8Write(dev, RELAY8_OUTPORT_REG_ADD, buff, 1);
		break;
	default:
		printf("Invalid relay state!\n");
		return ERROR;
		break;
	}
	return resp;
}

int relayChGet(int dev, u8 channel, OutStateEnumType* state)
{
	u8 buff[2];

	if (NULL == state)
	{
		return ERROR;
	}

	if ( (channel < CHANNEL_NR_MIN) || (channel > RELAY_CH_NR_MAX))
	{
		printf("Invalid relay nr!\n");
		return ERROR;
	}

	if (FAIL == i2cMem8Read(dev, RELAY8_INPORT_REG_ADD, buff, 1))
	{
		return ERROR;
	}

	if (buff[0] & (1 << relayChRemap[channel - 1]))
	{
		*state = ON;
	}
	else
	{
		*state = OFF;
	}
	return OK;
}

int relaySet(int dev, int val)
{
	u8 buff[2];

	buff[0] = relayToIO(0xff & val);

	return i2cMem8Write(dev, RELAY8_OUTPORT_REG_ADD, buff, 1);
}

int relayGet(int dev, int* val)
{
	u8 buff[2];

	if (NULL == val)
	{
		return ERROR;
	}
	if (FAIL == i2cMem8Read(dev, RELAY8_INPORT_REG_ADD, buff, 1))
	{
		return ERROR;
	}
	*val = IOToRelay(buff[0]);
	return OK;
}

int inChGet(int dev, u8 channel, OutStateEnumType* state)
{
	u8 buff[2];

	if (NULL == state)
	{
		return ERROR;
	}

	if ( (channel < CHANNEL_NR_MIN) || (channel > RELAY_CH_NR_MAX))
	{
		printf("Invalid relay nr!\n");
		return ERROR;
	}

	if (FAIL == i2cMem8Read(dev, RELAY8_INPORT_REG_ADD, buff, 1))
	{
		return ERROR;
	}

	if ((buff[0] & (1 << inMaskRemap[channel - 1])) == 0)
	{
		*state = ON;
	}
	else
	{
		*state = OFF;
	}
	return OK;
}

int inGet(int dev, int* val)
{
	u8 buff[2];

	if (NULL == val)
	{
		return ERROR;
	}
	if (FAIL == i2cMem8Read(dev, RELAY8_INPORT_REG_ADD, buff, 1))
	{
		return ERROR;
	}
	*val = IOToIn(buff[0]);
	return OK;
}


int doBoardInit(int stack)
{
	int dev = 0;
	int add = 0;
	uint8_t buff[8];
	uint8_t st = 0;

	if ( (stack < 0) || (stack > 7))
	{
		printf("Invalid stack level [0..7]!");
		return ERROR;
	}
	st = (stack & 0x02) + (0x01 & (stack >> 2)) + (0x04 & (stack << 2));
	add = (st + RELAY8_HW_I2C_BASE_ADD) ^ 0x07;
	dev = i2cSetup(add);
	if (dev == -1)
	{
		return ERROR;
	}
	if (ERROR == i2cMem8Read(dev, RELAY8_CFG_REG_ADD, buff, 1))
	{
		printf("4-RELAY_PLUS card id %d not detected\n", stack);
		return ERROR;
	}
	if (buff[0] != 0x0f) //non initialized I/O Expander
	{
		// make 4 I/O pins input and 4 output 
		buff[0] = 0x0f;
		if (0 > i2cMem8Write(dev, RELAY8_CFG_REG_ADD, buff, 1))
		{
			return ERROR;
		}
		// put all pins in 0-logic state
		buff[0] = 0;
		if (0 > i2cMem8Write(dev, RELAY8_OUTPORT_REG_ADD, buff, 1))
		{
			return ERROR;
		}
	}

	return dev;
}

int boardCheck(int hwAdd)
{
	int dev = 0;
	uint8_t buff[8];

	hwAdd ^= 0x07;
	dev = i2cSetup(hwAdd);
	if (dev == -1)
	{
		return FAIL;
	}
	if (ERROR == i2cMem8Read(dev, RELAY8_CFG_REG_ADD, buff, 1))
	{
		return ERROR;
	}
	return OK;
}

/*
 * doRelayWrite:
 *	Write coresponding relay channel
 **************************************************************************************
 */
static void doRelayWrite(int argc, char *argv[])
{
	int pin = 0;
	OutStateEnumType state = STATE_COUNT;
	int val = 0;
	int dev = 0;
	OutStateEnumType stateR = STATE_COUNT;
	int valR = 0;
	int retry = 0;

	if ( (argc != 5) && (argc != 4))
	{
		printf("Usage: 4relplus <id> write <relay number> <on/off> \n");
		printf("Usage: 4relplus <id> write <relay reg value> \n");
		exit(1);
	}

	dev = doBoardInit(atoi(argv[1]));
	if (dev <= 0)
	{
		exit(1);
	}
	if (argc == 5)
	{
		pin = atoi(argv[3]);
		if ( (pin < CHANNEL_NR_MIN) || (pin > RELAY_CH_NR_MAX))
		{
			printf("Relay number value out of range\n");
			exit(1);
		}

		/**/if ( (strcasecmp(argv[4], "up") == 0)
			|| (strcasecmp(argv[4], "on") == 0))
			state = ON;
		else if ( (strcasecmp(argv[4], "down") == 0)
			|| (strcasecmp(argv[4], "off") == 0))
			state = OFF;
		else
		{
			if ( (atoi(argv[4]) >= STATE_COUNT) || (atoi(argv[4]) < 0))
			{
				printf("Invalid relay state!\n");
				exit(1);
			}
			state = (OutStateEnumType)atoi(argv[4]);
		}

		retry = RETRY_TIMES;

		while ( (retry > 0) && (stateR != state))
		{
			if (OK != relayChSet(dev, pin, state))
			{
				printf("Fail to write relay\n");
				exit(1);
			}
			if (OK != relayChGet(dev, pin, &stateR))
			{
				printf("Fail to read relay\n");
				exit(1);
			}
			retry--;
		}
#ifdef DEBUG_I
		if(retry < RETRY_TIMES)
		{
			printf("retry %d times\n", 3-retry);
		}
#endif
		if (retry == 0)
		{
			printf("Fail to write relay\n");
			exit(1);
		}
	}
	else
	{
		val = atoi(argv[3]);
		if (val < 0 || val > 255)
		{
			printf("Invalid relay value\n");
			exit(1);
		}

		retry = RETRY_TIMES;
		valR = -1;
		while ( (retry > 0) && (valR != val))
		{

			if (OK != relaySet(dev, val))
			{
				printf("Fail to write relay!\n");
				exit(1);
			}
			if (OK != relayGet(dev, &valR))
			{
				printf("Fail to read relay!\n");
				exit(1);
			}
		}
		if (retry == 0)
		{
			printf("Fail to write relay!\n");
			exit(1);
		}
	}
}

/*
 * doRelayRead:
 *	Read relay state
 ******************************************************************************************
 */
static void doRelayRead(int argc, char *argv[])
{
	int pin = 0;
	int val = 0;
	int dev = 0;
	OutStateEnumType state = STATE_COUNT;

	dev = doBoardInit(atoi(argv[1]));
	if (dev <= 0)
	{
		exit(1);
	}

	if (argc == 4)
	{
		pin = atoi(argv[3]);
		if ( (pin < CHANNEL_NR_MIN) || (pin > RELAY_CH_NR_MAX))
		{
			printf("Relay number value out of range!\n");
			exit(1);
		}

		if (OK != relayChGet(dev, pin, &state))
		{
			printf("Fail to read!\n");
			exit(1);
		}
		if (state != 0)
		{
			printf("1\n");
		}
		else
		{
			printf("0\n");
		}
	}
	else if (argc == 3)
	{
		if (OK != relayGet(dev, &val))
		{
			printf("Fail to read!\n");
			exit(1);
		}
		printf("%d\n", val);
	}
	else
	{
		printf("Usage: %s read relay value\n", argv[0]);
		exit(1);
	}
}

/*
 * doInRead:
 *	Read inputs state
 ******************************************************************************************
 */
static void doInRead(int argc, char *argv[])
{
	int pin = 0;
	int val = 0;
	int dev = 0;
	OutStateEnumType state = STATE_COUNT;

	dev = doBoardInit(atoi(argv[1]));
	if (dev <= 0)
	{
		exit(1);
	}

	if (argc == 4)
	{
		pin = atoi(argv[3]);
		if ( (pin < CHANNEL_NR_MIN) || (pin > IN_CH_NR_MAX))
		{
			printf("Input channel number value out of range!\n");
			exit(1);
		}

		if (OK != inChGet(dev, pin, &state))
		{
			printf("Fail to read!\n");
			exit(1);
		}
		if (state != 0)
		{
			printf("1\n");
		}
		else
		{
			printf("0\n");
		}
	}
	else if (argc == 3)
	{
		if (OK != inGet(dev, &val))
		{
			printf("Fail to read!\n");
			exit(1);
		}
		printf("%d\n", val);
	}
	else
	{
		printf("Usage: %s read inputs value\n", argv[0]);
		exit(1);
	}
}

static void doHelp(int argc, char *argv[])
{
	int i = 0;
	if (argc == 3)
	{
		for (i = 0; i < CMD_ARRAY_SIZE; i++)
		{
			if ( (gCmdArray[i].name != NULL))
			{
				if (strcasecmp(argv[2], gCmdArray[i].name) == 0)
				{
					printf("%s%s%s%s", gCmdArray[i].help, gCmdArray[i].usage1,
						gCmdArray[i].usage2, gCmdArray[i].example);
					break;
				}
			}
		}
		if (CMD_ARRAY_SIZE == i)
		{
			printf("Option \"%s\" not found\n", argv[2]);
			printf("%s: %s\n", argv[0], usage);
		}
	}
	else
	{
		printf("%s: %s\n", argv[0], usage);
	}
}

static void doVersion(int argc, char *argv[])
{
	UNUSED(argc);
	UNUSED(argv);
	printf("4relplus v%d.%d.%d Copyright (c) 2016 - 2021 Sequent Microsystems\n",
	VERSION_BASE, VERSION_MAJOR, VERSION_MINOR);
	printf("\nThis is free software with ABSOLUTELY NO WARRANTY.\n");
	printf("For details type: 4relplus -warranty\n");

}

static void doList(int argc, char *argv[])
{
	int ids[8];
	int i;
	int cnt = 0;
	u8 st = 0;

	UNUSED(argc);
	UNUSED(argv);

	for (i = 0; i < 8; i++)
	{
		st = (0x02 & i) + (0x01 & (i >> 2)) + (0x04 & (i << 2));
		if (boardCheck(RELAY8_HW_I2C_BASE_ADD + st) == OK)
		{
			ids[cnt] = i;
			cnt++;
		}
	}
	printf("%d board(s) detected\n", cnt);
	if (cnt > 0)
	{
		printf("Id:");
	}
	while (cnt > 0)
	{
		cnt--;
		printf(" %d", ids[cnt]);
	}
	printf("\n");
}

/* 
 * Self test for production
 */
static void doTest(int argc, char* argv[])
{
	int dev = 0;
	int i = 0;
	int retry = 0;
	int relVal;
	int valR;
	int relayResult = 0;
	FILE* file = NULL;
	const u8 relayOrder[RELAY_CH_NR_MAX] =
	{
		1,
		2,
		3,
		4};

	dev = doBoardInit(atoi(argv[1]));
	if (dev <= 0)
	{
		exit(1);
	}
	if (argc == 4)
	{
		file = fopen(argv[3], "w");
		if (!file)
		{
			printf("Fail to open result file\n");
			//return -1;
		}
	}
//relay test****************************
	if (strcasecmp(argv[2], "test") == 0)
	{
		relVal = 0;
		printf(
			"Are all relays and LEDs turning on and off in sequence?\nPress y for Yes or any key for No....");
		startThread();
		while (relayResult == 0)
		{
			for (i = 0; i < RELAY_CH_NR_MAX; i++)
			{
				relayResult = checkThreadResult();
				if (relayResult != 0)
				{
					break;
				}
				valR = 0;
				relVal = (u8)1 << (relayOrder[i] - 1);

				retry = RETRY_TIMES;
				while ( (retry > 0) && ( (valR & relVal) == 0))
				{
					if (OK != relayChSet(dev, relayOrder[i], ON))
					{
						retry = 0;
						break;
					}

					if (OK != relayGet(dev, &valR))
					{
						retry = 0;
					}
				}
				if (retry == 0)
				{
					printf("Fail to write relay\n");
					if (file)
						fclose(file);
					exit(1);
				}
				busyWait(150);
			}
			for (i = 0; i < RELAY_CH_NR_MAX; i++)
			{
				relayResult = checkThreadResult();
				if (relayResult != 0)
				{
					break;
				}
				valR = 0xff;
				relVal = (u8)1 << (relayOrder[i] - 1);
				retry = RETRY_TIMES;
				while ( (retry > 0) && ( (valR & relVal) != 0))
				{
					if (OK != relayChSet(dev, relayOrder[i], OFF))
					{
						retry = 0;
					}
					if (OK != relayGet(dev, &valR))
					{
						retry = 0;
					}
				}
				if (retry == 0)
				{
					printf("Fail to write relay!\n");
					if (file)
						fclose(file);
					exit(1);
				}
				busyWait(150);
			}
		}
	}
	if (relayResult == YES)
	{
		if (file)
		{
			fprintf(file, "Relay Test ............................ PASS\n");
		}
		else
		{
			printf("Relay Test ............................ PASS\n");
		}
	}
	else
	{
		if (file)
		{
			fprintf(file, "Relay Test ............................ FAIL!\n");
		}
		else
		{
			printf("Relay Test ............................ FAIL!\n");
		}
	}
	if (file)
	{
		fclose(file);
	}
	relaySet(dev, 0);
}

static void doWarranty(int argc UNU, char* argv[] UNU)
{
	printf("%s\n", warranty);
}

static void cliInit(void)
{
	int i = 0;

	memset(gCmdArray, 0, sizeof(CliCmdType) * CMD_ARRAY_SIZE);

	memcpy(&gCmdArray[i], &CMD_HELP, sizeof(CliCmdType));
	i++;
	memcpy(&gCmdArray[i], &CMD_WAR, sizeof(CliCmdType));
	i++;
	memcpy(&gCmdArray[i], &CMD_LIST, sizeof(CliCmdType));
	i++;
	memcpy(&gCmdArray[i], &CMD_WRITE, sizeof(CliCmdType));
	i++;
	memcpy(&gCmdArray[i], &CMD_READ, sizeof(CliCmdType));
	i++;
	memcpy(&gCmdArray[i], &CMD_IN_READ, sizeof(CliCmdType));
	i++;
	memcpy(&gCmdArray[i], &CMD_TEST, sizeof(CliCmdType));
	i++;
	memcpy(&gCmdArray[i], &CMD_VERSION, sizeof(CliCmdType));

}

int main(int argc, char *argv[])
{
	int i = 0;

	cliInit();

	if (argc == 1)
	{
		printf("%s\n", usage);
		return 1;
	}
	for (i = 0; i < CMD_ARRAY_SIZE; i++)
	{
		if ( (gCmdArray[i].name != NULL) && (gCmdArray[i].namePos < argc))
		{
			if (strcasecmp(argv[gCmdArray[i].namePos], gCmdArray[i].name) == 0)
			{
				gCmdArray[i].pFunc(argc, argv);
				return 0;
			}
		}
	}
	printf("Invalid command option\n");
	printf("%s\n", usage);

	return 0;
}