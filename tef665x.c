#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <stdarg.h>
#include <error.h>

#include "tef665x.h"

#define I2C_ADDRESS 0x64
#define I2C_DEV "/dev/i2c-3"
#define VERSION "0.1"

#define TEF665x_CMD_LEN_MAX	20
#define SET_SUCCESS 1
#define TEF665X_SPLIT_SIZE		24

#define TEF665x_REF_CLK		9216000	//reference clock frequency
#define TEF665x_IS_CRYSTAL_CLK	0	//crstal
#define TEF665x_IS_EXT_CLK	1	//external clock input

#define High_16bto8b(a)	((u8)((a) >> 8))
#define Low_16bto8b(a) 	((u8)(a)) 
#define Convert8bto16b(a)	((ushort)(((ushort)(*(a))) << 8 |((ushort)(*(a+1)))))

const u8 tef665x_patch_cmdTab1[] = {3,	0x1c,0x00,0x00};
const u8 tef665x_patch_cmdTab2[] = {3,	0x1c,0x00,0x74};
const u8 tef665x_patch_cmdTab3[] = {3,	0x1c,0x00,0x75};

#define DEBUG 1

#ifdef DEBUG
#define _debug(x, y) printf("function: %s,  %s : %d\n", __FUNCTION__, #x, y) 
#elif
#define _debug(x, y)
#endif

int tef665x_set_cmd(int i2c_file_desc, TEF665x_MODULE module, u8 cmd, int len, ...)
{
	int i, ret;
	u8 buf[TEF665x_CMD_LEN_MAX];
	ushort temp;
    va_list vArgs;

    va_start(vArgs, len);
		
	buf[0] = module;	//module,	FM/AM/APP
	buf[1] = cmd;		//cmd,		1,2,10,... 
	buf[2] = 0x01;	    //index, 	always 1

	for(i = 3; i < len; i++)
	{
		temp = va_arg(vArgs,int);	
		
		buf[i++] = High_16bto8b(temp);		
		buf[i] = Low_16bto8b(temp);		
	}
	
	va_end(vArgs);  
	
	ret = write(i2c_file_desc, buf, len);

	temp = (ret == len) ? 1 : 0;
	_debug("return value", temp);
	return temp;
}

int tef665x_get_cmd(int i2c_file_desc, u8 cmd, TEF665x_MODULE module, u8 *receive, int len)
{
	u8 temp;
	u8 buf[3];
	int ret;

	buf[0]= module;		//module,	FM/AM/APP
	buf[1]= cmd;		//cmd,		1,2,10,... 
	buf[2]= 1;	        //index, 	always 1

	write(i2c_file_desc,buf , 3);

	ret = read(i2c_file_desc, receive, len);
	temp = (ret == len) ? 1 : 0;
	_debug("return value", temp);
	return temp;	
}

/*
module 64 APPL
cmd 128 Get_Operation_Status | status
index 
1 status
	Device operation status
	0 = boot state; no command support
	1 = idle state
	2 = active state; radio standby
	3 = active state; FM
	4 = active state; AM
*/
int appl_get_operation_status(int i2c_file_desc ,u8 *status)
{
   	u8 buf[2];
	int ret;

    ret = tef665x_get_cmd(i2c_file_desc, TEF665X_MODULE_APPL,
			TEF665X_Cmd_Get_Operation_Status,
			buf, sizeof(buf));

	if(ret == SET_SUCCESS)
	{
		*status = Convert8bto16b(buf);
		_debug("return value", 1);
		return 1;
	}
	_debug("return value", 0);
	return 0;
}

int get_operation_status(int i2c_file_desc, TEF665x_STATE *status)
{
	TEF665x_STATE data;
	int ret;
	if(SET_SUCCESS ==(ret = appl_get_operation_status(i2c_file_desc, &data)))
	{
		//printk( "appl_get_operation_status1 data= %d \n",data);
		_debug("got status", ret);
		switch(data)
		{
			case 0:
				_debug("status: boot", ret);
				*status = eDevTEF665x_Boot_state;
				break;
			case 1:
				_debug("status: idle", ret);
				*status = eDevTEF665x_Idle_state;
				break;
			default:
				_debug("status: active", ret);
				*status = eDevTEF665x_Active_state;
				break;
		}
	}
	return ret;
}

int tef665x_power_on(int i2c_file_desc)
{
	int ret;
	TEF665x_STATE status;
	usleep(5000);
	if(SET_SUCCESS == (ret = get_operation_status(i2c_file_desc, &status)))   //[ w 40 80 01 [ r 0000 ]
	{
		_debug("Powered ON", ret);
	}
	else
	{
		_debug("Powered ON FAILED!", ret);
	}

	return ret;
}

int tef665x_writeTab(int i2c_file_desc,const u8 *tab)
{
	int ret;
	ret = write(i2c_file_desc, tab + 1, tab[0]);
	return (ret != tab[0]) ? 0 : 1;
}

int tef665x_patch_load(int i2c_file_desc, const u8 *bytes, ushort size)
{
	u8 buf[25]; //the size which we break the data into, is 24 bytes.
	int ret, i;

    ushort num = size / 24;
	ushort rem = size % 24;

    buf[0] = 0x1b;

    usleep(10000);

    for(i = 0; i < num; i++)
    {
		memcpy(buf + 1, bytes + (24 * i), 24);

		ret = write(i2c_file_desc, buf, 25);

		if(ret != 25)
		{
			_debug("FAILED, send patch error! in pack no", i);
			return false;
		}
		usleep(50);
	}

    memcpy(buf + 1, bytes + (num * 24), rem);

    ret = write(i2c_file_desc, buf, rem);
		if(ret != rem)
		{
			_debug("FAILED, send patch error at the end!", 0);
			return false;
		}
	usleep(50);
	
	_debug("return value", 1);
	return true;
}

#if 0
//static int devTEF665x_Read(struct i2c_client *client, u8 reg,unsigned char * buf,u32 len)
static int devTEF665x_Read(struct i2c_client *client, u8 reg,unsigned char * buf,u32 len)
{ 
	int ret;

	ret = i2c_master_recv(client,buf,len);

	if(ret < 0)
	{
		printk("recv command error!!\n");
		return 0;
	}
	
	return 1;
}

//static int devTEF665x_Write(struct i2c_client *client,unsigned char * buf,u8 len)
static int devTEF665x_Write(struct i2c_client *client,unsigned char * buf,u8 len)
{
	int ret;

	ret = i2c_master_send(client,buf,len);

	if(ret < 0)static int devTEF665x_Read(struct i2c_client *client, u8 reg,unsigned char * buf,u32 len)

	{
		printk("sends command error!!\n");
		return 0;
	}
	
	return 1;unsigned char
	
}
#endif

int tef665x_patch_init(int i2c_file_desc)
{
	int ret = 0;
	ret = tef665x_writeTab(i2c_file_desc, tef665x_patch_cmdTab1);  //[ w 1C 0000 ]
	if(!ret)
	{
		_debug("1- tab1 load FAILED", ret);
		return ret;
	}

	ret = tef665x_writeTab(i2c_file_desc, tef665x_patch_cmdTab2);  //[ w 1C 0074 ]
	if(!ret)
	{
		_debug("2- tab2 load FAILED", ret);
		return ret;
	}

	ret = tef665x_patch_load(i2c_file_desc, pPatchBytes, patchSize); //table1	
	if(!ret)
	{
		_debug("3- pPatchBytes load FAILED", ret);
		return ret;
	}		
	
	ret = tef665x_writeTab(i2c_file_desc, tef665x_patch_cmdTab1); //[ w 1C 0000 ]	
	if(!ret)
	{
		_debug("4- tab1 load FAILED", ret);
		return ret;
	}	

	ret = tef665x_writeTab(i2c_file_desc, tef665x_patch_cmdTab3); //[ w 1C 0075 ]	
	if(!ret)
	{
		_debug("5- tab3 load FAILED", ret);
		return ret;
	}	

	ret = tef665x_patch_load(i2c_file_desc, pLutBytes, lutSize); //table2	
	if(!ret)
	{
		_debug("6- pLutBytes load FAILED", ret);
		return ret;
	}		

	ret = tef665x_writeTab(i2c_file_desc, tef665x_patch_cmdTab1); //[ w 1C 0000 ]	
	if(!ret)
	{
		_debug("7- tab1 load FAILED", ret);
		return ret;
	}	
	_debug("patch loaded", ret);
	return ret;	
}

//Command start will bring the device into? idle state�: [ w 14 0001 ]
int tef665x_start_cmd(int i2c_file_desc)
{

	int ret;
	unsigned char  buf[3];
	
	buf[0] = 0x14;
	buf[1] = 0;
	buf[2] = 1;

	ret = write(i2c_file_desc, buf, 3);

	if (ret != 3)
	{
		_debug("start cmd FAILED", 0);
		return 0;
	}
	_debug("return true", 1);
	return 1;
}

int tef665x_boot_state(int i2c_file_desc)
{
	int ret=0;
	if(1 == tef665x_patch_init(i2c_file_desc))
	{
		_debug("return true", 1);
	}
	else 
	{
		_debug("return value", 0);
		return 0;
	}

	usleep(50000);

	if(1 == tef665x_start_cmd(i2c_file_desc))
	{
		_debug("'start cmd'return true", 1);
	}
	else 
	{
		_debug("return value", 0);
		return 0;
	}
	
	usleep(50000);
	
	return ret;
}

/*
module 64 APPL
cmd 4 Set_ReferenceClock frequency

index 
1 frequency_high
	[ 15:0 ]
	MSB part of the reference clock frequency
	[ 31:16 ]
2 frequency_low
	[ 15:0 ]
	LSB part of the reference clock frequency
	[ 15:0 ]
	frequency [*1 Hz] (default = 9216000)
3 type
	[ 15:0 ]
	clock type
	0 = crystal oscillator operation (default)
	1 = external clock input operation
*/
int tef665x_appl_set_referenceClock(uint i2c_file_desc, ushort frequency_high, ushort frequency_low, ushort type)
{
	return tef665x_set_cmd(i2c_file_desc, TEF665X_MODULE_APPL,
			TEF665X_Cmd_Set_ReferenceClock, 
			9,
			frequency_high, frequency_low, type);
}

int appl_set_referenceClock(uint i2c_file_desc, uint frequency, bool is_ext_clk)  //0x3d 0x900
{
	return tef665x_appl_set_referenceClock(i2c_file_desc,(ushort)(frequency >> 16), (ushort)frequency, is_ext_clk);
}

/*
module 64 APPL
cmd 5 Activate mode

index 
1 mode
	[ 15:0 ]
	1 = goto �active� state with operation mode of �radio standby�
*/
int tef665x_appl_activate(uint i2c_file_desc ,ushort mode)
{
	return tef665x_set_cmd(i2c_file_desc, TEF665X_MODULE_APPL,
			TEF665X_Cmd_Activate, 
			5,
			mode);
}

int appl_activate(uint i2c_file_desc)
{
	return tef665x_appl_activate(i2c_file_desc, 1);
}

int tef665x_idle_state(int i2c_file_desc)
{

	TEF665x_STATE status;
	
	//mdelay(50);

	if(SET_SUCCESS == get_operation_status(i2c_file_desc, &status))
	{
		_debug("got operation status", 1);	
 	    if(status != eDevTEF665x_Boot_state)
		{
			_debug("not in boot status", 1);
			if(SET_SUCCESS == appl_set_referenceClock(i2c_file_desc, TEF665x_REF_CLK, TEF665x_IS_CRYSTAL_CLK)) //TEF665x_IS_EXT_CLK
			{
				_debug("set the clock", TEF665x_REF_CLK);
				if(SET_SUCCESS == appl_activate(i2c_file_desc))// APPL_Activate mode = 1.[ w 40 05 01 0001 ]
				{
					//usleep(100000); //Wait 100 ms
					_debug("activate succeed", 1);	
					return 1;
				}
				else
				{
					_debug("activate FAILED", 1);
				}
			}
			else
			{
				_debug("set the clock FAILED", TEF665x_REF_CLK);
			}
			
		}
		else
		{
			_debug("did not get operation status", 0);
		}
		
	}
	_debug("return value", 0);
	return 0;
}

int tef665x_para_load(uint i2c_file_desc)
{
	int i;
	int r;
	const u8 *p = init_para;

	for(i = 0; i < sizeof(init_para); i += (p[i]+1))
	{
		if(SET_SUCCESS != (r = tef665x_writeTab(i2c_file_desc, p + i)))
		{
			break;
		}
	}
	
	_debug("return value", r);
	return r;
}

/*
module 32 / 33 FM / AM
cmd 1 Tune_To mode, frequency

index 
1 mode
	[ 15:0 ]
	tuning actions
	0 = no action (radio mode does not change as function of module band)
	1 = Preset Tune to new program with short mute time
	2 = Search Tune to new program and stay muted
	FM 3 = AF-Update Tune to alternative frequency, store quality
	and tune back with inaudible mute
	4 = Jump Tune to alternative frequency with short
	inaudible mute
	5 = Check Tune to alternative frequency and stay
	muted
	AM 3 � 5 = reserved
	6 = reserved
	7 = End Release the mute of a Search or Check action
	(frequency is not required and ignored)
2 frequency
[ 15:0 ]
	tuning frequency
	FM 6500 � 10800 65.00 � 108.00 MHz / 10 kHz step size
	AM LW 144 � 288 144 � 288 kHz / 1 kHz step size
	MW 522 � 1710 522 � 1710 kHz / 1 kHz step size
	SW 2300 � 27000 2.3 � 27 MHz / 1 kHz step size
*/
int tef665x_radio_tune_to (uint i2c_file_desc, bool fm, ushort mode,ushort frequency )
{
	return tef665x_set_cmd(i2c_file_desc, fm ? TEF665X_MODULE_FM: TEF665X_MODULE_AM,
			TEF665X_Cmd_Tune_To, 
			( mode <= 5 ) ? 7 : 5,
			mode, frequency);
}

int FM_tune_to(uint i2c_file_desc, AR_TuningAction_t mode, ushort frequency)
{
	int ret = tef665x_radio_tune_to(i2c_file_desc, 1, (ushort)mode, frequency);
	_debug("return value", ret);
	return ret;
}

int AM_tune_to(uint i2c_file_desc, AR_TuningAction_t mode,ushort frequency)
{
	int ret = tef665x_radio_tune_to(i2c_file_desc, 0, (ushort)mode, frequency);
	_debug("return value", ret);
	return ret;
}

/*
module 48 AUDIO
cmd 11 Set_Mute mode

index 
1 mode
	[ 15:0 ]
	audio mute
	0 = mute disabled
	1 = mute active (default)
*/
int tef665x_audio_set_mute(uint i2c_file_desc, ushort mode)
{
	int ret = tef665x_set_cmd(i2c_file_desc, TEF665X_MODULE_AUDIO,
			  TEF665X_Cmd_Set_Mute, 
			  5,
			  mode);
	if(ret)
	{
		_debug("muted, mode", 1);
	}
	else
	{
		_debug("FAILED, return", 0);
		return 0;
	}
	return 1;
}

/*
module 48 AUDIO
cmd 10 Set_Volume volume

index 
1 volume
	[ 15:0 ] (signed)
	audio volume
	-599 � +240 = -60 � +24 dB volume
	0 = 0 dB (default)f665x_patch_init function:  "3"t,int16_t volume)
*/
int tef665x_audio_set_volume(uint i2c_file_desc, ushort volume)
{
	return tef665x_set_cmd(i2c_file_desc, TEF665X_MODULE_AUDIO,
			TEF665X_Cmd_Set_Volume,
			5,
			volume*10);
}

//mute=1, unmute=0
int audio_set_mute(uint i2c_file_desc, bool mute)
{
	return tef665x_audio_set_mute(i2c_file_desc, mute);//AUDIO_Set_Mute mode = 0 : disable mute
}

//-60 � +24 dB volume
int audio_set_volume(uint i2c_file_desc, int vol)
{
	return tef665x_audio_set_volume(i2c_file_desc, (ushort)vol);
}

/*
module 64 APPL
cmd 1 Set_OperationMode mode

index 
1 mode
	[ 15:0 ]
	device operation mode
	0 = normal operation
	1 = radio standby mode (low-power mode without radio functionality)
	(default)
*/

int tef665x_audio_set_operationMode(uint i2c_file_desc, ushort mode)
{
	_debug("normal: 0   standby: 1   requested", 1);
	int ret = tef665x_set_cmd(i2c_file_desc, TEF665X_MODULE_APPL,
			  TEF665X_Cmd_Set_OperationMode, 
			  5,
			  mode);
	if(ret)
	{
		_debug("was able to set the mode", ret);
	}
	else
	{
		_debug("FAILED, return", 0);
		return 0;
	}
	return 1;
}

//TRUE = ON;
//FALSE = OFF
void radio_powerSwitch(uint i2c_file_desc, bool OnOff)
{
	tef665x_audio_set_operationMode(i2c_file_desc, OnOff? 0 : 1);//standby mode = 1
}

void radio_modeSwitch(uint i2c_file_desc, bool mode_switch, AR_TuningAction_t mode, ushort frequency)
{
	
	if(mode_switch)	//FM
	{
		FM_tune_to(i2c_file_desc, mode, frequency);
	}
	else //AM
	{
		AM_tune_to(i2c_file_desc, mode, frequency);
	}
}

int tef665x_wait_active(uint i2c_file_desc)
{
	TEF665x_STATE status;
	//usleep(50000);
	if(SET_SUCCESS == appl_get_operation_status(i2c_file_desc, &status))
	{
		_debug("got status", 1);
		if((status != eDevTEF665x_Boot_state) && (status != eDevTEF665x_Idle_state))
		{
			_debug("active status", 1);
			if(SET_SUCCESS == tef665x_para_load(i2c_file_desc))
			{
				_debug("parameters loaded", 1);
			}
			else
			{
				_debug("parameters not loaded", 0);
				return 0;
			}
			
			FM_tune_to(i2c_file_desc, eAR_TuningAction_Preset, 9350);// tune to 93.5MHz

			if(SET_SUCCESS == audio_set_mute(i2c_file_desc, 1))//unmute=0
			{
				_debug("muted", 1);
			}
			else
			{
				_debug("not muted", 0);
				return 0;
			}
			
			if(SET_SUCCESS == audio_set_volume(i2c_file_desc, -25))//set to -25db
			{
				_debug("set vol to", -25);
			}
			else
			{
				_debug("vol not set", 0);
				return 0;
			}
			return 1;
		}
	}

	return 0;
}

void tef665x_chip_init(int i2c_file_desc)
{
	if(1 == tef665x_power_on(i2c_file_desc)) _debug("tef665x_power_on", 1);
	usleep(50000);
	if(1 == tef665x_boot_state(i2c_file_desc)) _debug("tef665x_boot_state", 1);
	usleep(100000);
	if(1 == tef665x_idle_state(i2c_file_desc)) _debug("tef665x_idle_state", 1);
	usleep(200000);
	if(1 == tef665x_wait_active(i2c_file_desc)) _debug("tef665x_wait_active", 1);
}
void usage(void)
{
	printf("\nUsage: tef665x <AM/FM> frequency in integer: <dddd>\n");
	printf("example (FM and 93.5MHz): ./tef665x FM 9350\n\n");
}

int main(int argc, char *argv[])
{
    int fd, t;
	int band;
	ushort freq;

	if(argc != 3)
	{
		usage();
		return 1;
	}
	else
	{
		if(!strcmp(argv[1], "AM"))
		{
			band = 0;
		}
		else if(!strcmp(argv[1], "FM"))
		{
			band = 1;	
		}
		else
		{
			usage();
			return 1;
		}
		
		freq = (ushort)strtod(argv[2], NULL);
		if(freq == 0)
		{
			usage();
			return 1;
		}		
	}
	
	fd = open(I2C_DEV, O_RDWR);
	if(fd < 0)
	{
		printf("could not open %s", I2C_DEV);
		return fd;
	}

    t = ioctl(fd, I2C_SLAVE, I2C_ADDRESS);
	if (t < 0)
	{
		printf("could not set up slave ");
		return t;
	}

	printf("\nThis SW would help you test TEF665x tuner chip on your board! \n\n");
	printf("Plan is to play for 10 sec\n");
	printf("Version %s\n\n", VERSION);

	radio_powerSwitch(fd,1);

	tef665x_chip_init(fd);

	radio_modeSwitch(fd, band, eAR_TuningAction_Preset, freq);
	
	audio_set_mute(fd, 0);

	sleep(10);

	audio_set_mute(fd, 1);

    close(fd);

    return 0;  
}
