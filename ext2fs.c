#include <stdio.h>
#include <conio.h>
#include <windows.h>
#include <time.h>
char dev_path_buf[4096];
char *dev_path;
unsigned char mode=0;
void ext2_sync(unsigned char mode);
void err_quit(void)
{
	if(mode)
	{
		ext2_sync(1);
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
struct spinlock
{
	unsigned char lock[1];
};
void spin_lock(struct spinlock *lock)
{
	while(lock_set8(lock->lock,1));
}
void spin_unlock(struct spinlock *lock)
{
	lock_set8(lock->lock,0);
}
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
