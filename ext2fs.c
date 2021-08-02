#include <stdio.h>
#include <conio.h>
#include <windows.h>
char dev_path_buf[4096];
char *dev_path;
unsigned char mode=0;
void err_quit(void)
{
	if(mode)
	{
		ext2_sync();
	}
	printf("\nPress any key to continue.\n");
	getch();
	exit(0);
}
unsigned char lock_set8(void *ptr,unsigned char val);
asm("lock_set8:");
asm("xchg %dl,(%rcx)");
asm("mov %dl,%al");
asm("ret");
void input_path_name(void)
{
	int x=0;
	char c;
	while(x<4095)
	{
		c=getchar();
		if(c==10)
		{
			return;
		}
		dev_path_buf[x]=c;
		x++;
	}
	while(c!=10)
	{
		c=getchar();
	}
}
#include "ext2.c"
#include "cmdline.c"
int main(int argc,char *argv[])
{
	unsigned char buf[4096];
	struct file *root;
	unsigned int fi;
	unsigned long long int x=0;
	if(argc<2)
	{
		printf("Input device name (e.g. \"\\\\.\\D:\"): ");
		input_path_name();
		dev_path=dev_path_buf;
	}
	else
	{
		dev_path=argv[1];
	}
	ext2_init();
	mode=1;
	printf("Type \"help\" for available commands.\n");
	while(1)
	{
		printf("EXT2FS>> ");
		cmd_run();
	}
}
