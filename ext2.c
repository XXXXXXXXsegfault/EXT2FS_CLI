struct ext2_superblock
{
	unsigned int inodes;
	unsigned int blocks;
	unsigned int r_blocks;
	unsigned int free_blocks;
	unsigned int free_inodes;
	unsigned int first_data_block;
	unsigned int block_size;
	unsigned int frag_size;
	unsigned int blocks_per_group;
	unsigned int frags_per_group;
	unsigned int inodes_per_group;
	unsigned int mtime;
	unsigned int wtime;
	unsigned short int mounts;
	unsigned short int max_mounts;
	unsigned short int magic;
	unsigned short int state;
	unsigned short int error_handling;
	unsigned short int ver_min;
	unsigned int last_ck;
	unsigned int interval_ck;
	unsigned int os_id;
	unsigned int ver_maj;
	unsigned short int uid;
	unsigned short int gid;
	unsigned int first_inode;
	unsigned short int inode_size;
	unsigned short int bg_num;
	unsigned int features_compat;
	unsigned int features_incompat;
	unsigned int features_ro_compat;
	unsigned char uuid[16];
	char volume_name[16];
	char last_mp[64];
	unsigned int pad[206];
} sb;
struct ext2_bgdt
{
	unsigned int block_bitmap;
	unsigned int inode_bitmap;
	unsigned int inode_table;
	unsigned short int free_blocks;
	unsigned short int free_inodes;
	unsigned short int used_dirs;
	unsigned char rsv[14];
} *gt;
struct ext2_inode
{
	unsigned short int mode;
	unsigned short int uid;
	unsigned int size;
	unsigned int atime;
	unsigned int ctime;
	unsigned int mtime;
	unsigned int dtime;
	unsigned short int gid;
	unsigned short int links;
	unsigned int blocks;
	unsigned int flags;
	unsigned int osd1;
	unsigned int block[15];
	unsigned int gen;
	unsigned int file_acl;
	unsigned int dir_acl; //file size high
	unsigned int faddr;
	unsigned int osd[3];
};
#define file_size(inode) (((inode)->mode&0170000)==040000?(unsigned long long int)(inode)->size:(unsigned long long int)(inode)->dir_acl<<32|(inode)->size)
void file_size_set(struct ext2_inode *inode,unsigned long long int size)
{
	inode->size=size;
	if((inode->mode&0170000)!=040000)
	{
		inode->dir_acl=size>>32;
	}
}
struct ext2_dirent
{
	unsigned int inode;
	unsigned short int rec_len;
	unsigned char name_len;
	unsigned char file_type;
	char name[1];
};
unsigned char sb_changed=0;
unsigned char sb_buf[4096];
unsigned long long int devsize;
HANDLE hDev;
unsigned int groups;
unsigned char dev_io_lock=0;
void read_raw_blocks(unsigned int off,void *ptr,unsigned int count)
{
	LARGE_INTEGER off_b={0};
	unsigned int size=count<<sb.block_size+10,buf=0;
	off_b.QuadPart=(unsigned long long int)off<<sb.block_size+10;
	if(off_b.QuadPart>=devsize)
	{
		return;
	}
	if(off_b.QuadPart+size>devsize)
	{
		size=devsize-off_b.QuadPart;
	}
	while(lock_set8(&dev_io_lock,1));
	SetFilePointerEx(hDev,off_b,NULL,FILE_BEGIN);
	ReadFile(hDev,ptr,size,&buf,NULL);
	dev_io_lock=0;
	if(buf!=size)
	{
		printf("Fatal error: I/O error while reading block %u, %u.\n",off,count);
		err_quit();
	}
}
void write_raw_blocks(unsigned int off,void *ptr,unsigned int count)
{
	LARGE_INTEGER off_b={0};
	unsigned int size=count<<sb.block_size+10,buf=0;
	off_b.QuadPart=(unsigned long long int)off<<sb.block_size+10;
	if(off_b.QuadPart>=devsize)
	{
		return;
	}
	if(off_b.QuadPart+size>devsize)
	{
		size=devsize-off_b.QuadPart;
	}
	while(lock_set8(&dev_io_lock,1));
	SetFilePointerEx(hDev,off_b,NULL,FILE_BEGIN);
	WriteFile(hDev,ptr,size,&buf,NULL);
	dev_io_lock=0;
	if(buf!=size)
	{
		printf("Fatal error: I/O error while writing block %u, %u.\n",off,count);
		err_quit();
	}
}
void save_sb(void)
{
	unsigned char m=0;
	static unsigned char *buf;
	static unsigned int buf_size;
	if(!m)
	{
		buf_size=(groups-1>>5+sb.block_size)+2;
		if(!sb.block_size)
		{
			buf_size++;
		}
		
		if((buf=malloc(buf_size<<sb.block_size+10))==NULL)
		{
			printf("Fatal error: Cannot allocate memory.\n");
			err_quit();
		}
		m=1;
	}
	read_raw_blocks(0,buf,buf_size);
	memcpy(buf+1024,&sb,1024);
	if(sb.block_size)
	{
		memcpy(buf+(1<<sb.block_size+10),gt,groups*32);
	}
	else
	{
		memcpy(buf+2048,gt,groups*32);
	}
	write_raw_blocks(0,buf,buf_size);
}
unsigned char ext2_io_lock=0;
#define CACHE_PAGES 32
#define CACHE_PAGE_BLOCKS 4096
unsigned int p_rand(unsigned int max)
{
	static unsigned int t=1234;
	unsigned int a[64]={0x671d8983,0x0c14344a,0xd14964f6,0xcb7123df};
	unsigned int x=4;
	unsigned int result=0;
	t+=clock()+0x13505e7d;
	a[0]^=t;
	a[1]^=t;
	a[2]^=t;
	a[3]^=t;
	while(x<64)
	{
		a[x]=(a[x-4]&a[x-3])^(a[x-2]&a[x-1])^~a[x-3];
		x++;
	}
	x=0;
	while(x<64)
	{
		result=result&(a[x]>>20|a[x]<<12);
		result=result|(a[x]>>5|a[x]<<27);
		result=result+(a[x]>>15|a[x]<<17);
		result=result^(a[x]>>13|a[x]<<19);
		x++;
	}
	return result%max;
}
int chance(unsigned int m,unsigned int n)
{
	if(m>=n)
	{
		return 1;
	}
	if(m==0)
	{
		return 0;
	}
	if(p_rand(n)<m)
	{
		return 1;
	}
	return 0;
}
struct ext2_cache_struct
{
	unsigned int bitmap2[CACHE_PAGE_BLOCKS/32];
	unsigned int bitmap[CACHE_PAGE_BLOCKS/32];
	unsigned int start;
	unsigned char *data;
} ext2_cache[CACHE_PAGES]={0};
void ext2_sync(unsigned char mode)
{
	static unsigned int x=0;
	unsigned int y,y1,l;
	while(lock_set8(&ext2_io_lock,1));
	if(mode)
	{
		x=0;
	}
X1:
	if(ext2_cache[x].data)
	{
		y=0;
		l=0;
		while(y<CACHE_PAGE_BLOCKS/32)
		{
			if(ext2_cache[x].bitmap[y])
			{
				if(!l)
				{
					y1=y;
				}
				l++;
				ext2_cache[x].bitmap[y]=0;
			}
			else
			{
				if(l)
				{
					write_raw_blocks(ext2_cache[x].start+y1*32,ext2_cache[x].data+(y1*32<<sb.block_size+10),l*32);
				}
				l=0;
			}
			y++;
		}
		if(l)
		{
			write_raw_blocks(ext2_cache[x].start+y1*32,ext2_cache[x].data+(y1*32<<sb.block_size+10),l*32);
		}
		if(mode||chance(1,16));
		{
			free(ext2_cache[x].data);
			ext2_cache[x].data=NULL;
			memset(ext2_cache[x].bitmap2,0,sizeof(ext2_cache[x].bitmap2));
		}
	}
	if(mode)
	{
		if(x<CACHE_PAGES-1)
		{
			x++;
			goto X1;
		}
		else
		{
			if(lock_set8(&sb_changed,0))
			{
				save_sb();
			}
			ext2_io_lock=0;
			return;
		}
	}
	if(chance(1,256))
	{
		if(lock_set8(&sb_changed,0))
		{
			save_sb();
		}
	}
	x++;
	if(x==CACHE_PAGES)
	{
		x=0;
	}
	ext2_io_lock=0;
}
DWORD WINAPI T_ext2_io(LPVOID lpParameter)
{
	while(1)
	{
		ext2_sync(0);
		Sleep(300);
	}
}
void read_block(unsigned int start,void *ptr)
{
	int start1=start&~(CACHE_PAGE_BLOCKS-1);
	int x=0,x1=-1;
	if(start==0)
	{
		memset(ptr,0,1<<sb.block_size+10);
		return;
	}
	while(lock_set8(&ext2_io_lock,1));
	
	while(x<CACHE_PAGES)
	{
		if(ext2_cache[x].data)
		{
			if(ext2_cache[x].start==start1)
			{
				if(!ext2_cache[x].bitmap2[start-start1>>5])
				{
					read_raw_blocks(start1+((start-start1>>5)<<5),ext2_cache[x].data+((start-start1>>5)<<sb.block_size+15),32);
				}
				memcpy(ptr,ext2_cache[x].data+(start-start1<<sb.block_size+10),1<<sb.block_size+10);
				ext2_cache[x].bitmap2[start-start1>>5]|=1<<(start-start1&31);
				ext2_io_lock=0;
				return;
			}
		}
		else
		{
			x1=x;
		}
		x++;
	}
	if(x1!=-1&&(ext2_cache[x1].data=malloc(CACHE_PAGE_BLOCKS<<sb.block_size+10)))
	{
		read_raw_blocks(start1+((start-start1>>5)<<5),ext2_cache[x1].data+((start-start1>>5)<<sb.block_size+15),32);
		ext2_cache[x1].start=start1;
		memcpy(ptr,ext2_cache[x1].data+(start-start1<<sb.block_size+10),1<<sb.block_size+10);
		ext2_cache[x1].bitmap2[start-start1>>5]|=1<<(start-start1&31);
		ext2_io_lock=0;
	}
	else
	{
		read_raw_blocks(start,ptr,1);
		ext2_io_lock=0;
	}
}
void write_block(unsigned int start,void *ptr)
{
	int start1=start&~(CACHE_PAGE_BLOCKS-1);
	int x=0,x1=-1;
	if(start==0)
	{
		return;
	}
	while(lock_set8(&ext2_io_lock,1));
	while(x<CACHE_PAGES)
	{
		if(ext2_cache[x].data)
		{
			if(ext2_cache[x].start==start1)
			{
				if(!ext2_cache[x].bitmap2[start-start1>>5])
				{
					read_raw_blocks(start1+((start-start1>>5)<<5),ext2_cache[x].data+((start-start1>>5)<<sb.block_size+15),32);
				}
				memcpy(ext2_cache[x].data+(start-start1<<sb.block_size+10),ptr,1<<sb.block_size+10);
				ext2_cache[x].bitmap2[start-start1>>5]|=1<<(start-start1&31);
				ext2_cache[x].bitmap[start-start1>>5]|=1<<(start-start1&31);
				ext2_io_lock=0;
				return;
			}
		}
		else
		{
			x1=x;
		}
		x++;
	}
	if(x1!=-1&&(ext2_cache[x1].data=malloc(CACHE_PAGE_BLOCKS<<sb.block_size+10)))
	{
		read_raw_blocks(start1+((start-start1>>5)<<5),ext2_cache[x1].data+((start-start1>>5)<<sb.block_size+15),32);
		ext2_cache[x1].start=start1;
		memcpy(ext2_cache[x1].data+(start-start1<<sb.block_size+10),ptr,1<<sb.block_size+10);
		ext2_cache[x1].bitmap2[start-start1>>5]|=1<<(start-start1&31);
		ext2_cache[x1].bitmap[start-start1>>5]|=1<<(start-start1&31);
		ext2_io_lock=0;
	}
	else
	{
		write_raw_blocks(start,ptr,1);
		ext2_io_lock=0;
	}
}
struct file
{
	struct ext2_inode inode;
	unsigned char lock;
	unsigned char mode;
	unsigned int ninode;
};
#define FILE_MODE_RO 0
#define FILE_MODE_RW 1
void inode_read(unsigned int ninode,struct ext2_inode *inode)
{
	static char buf[4096];
	unsigned int group=(ninode-1)/sb.inodes_per_group,start,off=(ninode-1)%sb.inodes_per_group*sb.inode_size,index;
	index=off>>sb.block_size+10;
	start=gt[group].inode_table+index;
	off-=index<<sb.block_size+10;
	read_block(start,buf);
	memcpy(inode,buf+off,128);
}
void inode_write(unsigned int ninode,struct ext2_inode *inode)
{
	static char buf[4096];
	unsigned int group=(ninode-1)/sb.inodes_per_group,start,off=(ninode-1)%sb.inodes_per_group*sb.inode_size,index;
	index=off>>sb.block_size+10;
	start=gt[group].inode_table+index;
	off-=index<<sb.block_size+10;
	read_block(start,buf);
	memset(buf+off,0,sb.inode_size);
	memcpy(buf+off,inode,128);
	write_block(start,buf);
}
struct file *file_load(unsigned int ninode,unsigned char mode)
{
	struct file *t;
	
	if(ninode>sb.inodes||(t=malloc(sizeof(*t)))==NULL)
	{
		return NULL;
	}
	memset(t,0,sizeof(*t));
	t->ninode=ninode;
	t->mode=mode;
	inode_read(ninode,&t->inode);
	return t;
}
void file_release(struct file *fp)
{
	if(fp->mode==FILE_MODE_RW)
	{
		inode_write(fp->ninode,&fp->inode);
	}
	free(fp);
}
unsigned int ext2_ind_block(unsigned int iblock,unsigned int index)
{
	static unsigned int buf[1024];
	memset(buf,0,4096);
	if(iblock==0)
	{
		return 0;
	}
	read_block(iblock,buf);
	return buf[index];
}
int file_read_block(struct file *fp,unsigned int start,void *ptr)
{
	unsigned long long int size=file_size(&fp->inode);
	unsigned int nblock,size1=8+sb.block_size;
	if(size==0||start>=(size-1>>10+sb.block_size)+1)
	{
		return 0;
	}
	if(start<12)
	{
		nblock=fp->inode.block[start];
		goto X1;
	}
	start-=12;
	if(start<1<<size1)
	{
		nblock=ext2_ind_block(fp->inode.block[12],start);
		goto X1;
	}
	start-=1<<size1;
	size1+=8+sb.block_size;
	if(start<1<<size1)
	{
		nblock=ext2_ind_block(fp->inode.block[13],start>>8+sb.block_size);
		nblock=ext2_ind_block(nblock,start&(1<<8+sb.block_size)-1);
		goto X1;
	}
	start-=1<<size1;
	size1+=8+sb.block_size;
	if(start<1<<size1)
	{
		nblock=ext2_ind_block(fp->inode.block[14],start>>(8+sb.block_size)*2);
		nblock=ext2_ind_block(nblock,(start&(1<<8+sb.block_size)*2-1)>>8+sb.block_size);
		nblock=ext2_ind_block(nblock,start&(1<<8+sb.block_size)-1);
		goto X1;
	}
	else
	{
		return 0;
	}
X1:
	read_block(nblock,ptr);
	return 1;
}
int file_write_block(struct file *fp,unsigned int start,void *ptr)
{
	unsigned long long int size=file_size(&fp->inode);
	unsigned int nblock,size1=8+sb.block_size;
	if(size==0||start>=(size-1>>10+sb.block_size)+1)
	{
		return 0;
	}
	if(start<12)
	{
		nblock=fp->inode.block[start];
		goto X1;
	}
	start-=12;
	if(start<1<<size1)
	{
		nblock=ext2_ind_block(fp->inode.block[12],start);
		goto X1;
	}
	start-=1<<size1;
	size1+=8+sb.block_size;
	if(start<1<<size1)
	{
		nblock=ext2_ind_block(fp->inode.block[13],start>>8+sb.block_size);
		nblock=ext2_ind_block(nblock,start&(1<<8+sb.block_size)-1);
		goto X1;
	}
	start-=1<<size1;
	size1+=8+sb.block_size;
	if(start<1<<size1)
	{
		nblock=ext2_ind_block(fp->inode.block[14],start>>(8+sb.block_size)*2);
		nblock=ext2_ind_block(nblock,(start&(1<<8+sb.block_size)*2-1)>>8+sb.block_size);
		nblock=ext2_ind_block(nblock,start&(1<<8+sb.block_size)-1);
		goto X1;
	}
	else
	{
		return 0;
	}
X1:
	if(nblock==0)
	{
		return 0;
	}
	write_block(nblock,ptr);
	return 1;
}
int file_read(struct file *fp,unsigned long long int start,int size,void *ptr)
{
	int b_read=0;
	unsigned int size1,bstart=start>>10+sb.block_size,off=start&(1<<10+sb.block_size)-1;
	unsigned long long int fsize=file_size(&fp->inode);
	static unsigned char buf[4096];
	if(start>=fsize)
	{
		return 0;
	}
	if(start+size>fsize)
	{
		size=fsize-start;
	}
	while(size>0)
	{
		if(size>(1<<sb.block_size+10)-off)
		{
			size1=(1<<sb.block_size+10)-off;
		}
		else
		{
			size1=size;
		}
		if(file_read_block(fp,bstart,buf)==0)
		{
			break;
		}
		memcpy(ptr,buf+off,size1);
		bstart++;
		size-=size1;
		b_read+=size1;
		ptr=(char *)ptr+size1;
		off=0;
	}
	return b_read;
}

unsigned int file_readdir(struct file *fp,unsigned int off,struct ext2_dirent *dir)
{
	if((fp->inode.mode&0170000)!=040000)
	{
		return 0;
	}
next:
	if(file_read(fp,off,8,dir)!=8)
	{
		return 0;
	}
	if(dir->inode==0)
	{
		off+=dir->rec_len;
		goto next;
	}
	if(file_read(fp,off,dir->name_len+8,dir)!=dir->name_len+8)
	{
		return 0;
	}
	return off+dir->rec_len;
}
unsigned int file_search(struct file *dirfp,char *name)
{
	unsigned char buf[264];
	struct ext2_dirent *dir=(void *)buf;
	unsigned int l=strlen(name),off=0;
	if(l>=256)
	{
		return 0;
	}
	while(off=file_readdir(dirfp,off,dir))
	{
		if(dir->name_len==l&&!memcmp(dir->name,name,l))
		{
			return dir->inode;
		}
	}
	return 0;
}
unsigned int ext2_block_alloc(unsigned int ninode)
{
	unsigned int ngroup=(ninode-1)/sb.inodes_per_group;
	unsigned int x=ngroup,x1;
	unsigned int bs=1<<sb.block_size+10;
	static unsigned char buf[4096];
	if(sb.free_blocks==0)
	{
		return 0;
	}
	do
	{
		if(gt[x].free_blocks!=0)
		{
			read_block(gt[x].block_bitmap,buf);
			x1=0;
			while(x1<sb.blocks_per_group)
			{
				if(!(buf[x1>>3]&1<<(x1&7)))
				{
					buf[x1>>3]|=1<<(x1&7);
					gt[x].free_blocks--;
					sb.free_blocks--;
					write_block(gt[x].block_bitmap,buf);
					lock_set8(&sb_changed,1);
					return x1+x*sb.blocks_per_group+(sb.block_size?0:1);
				}
				x1++;
			}
		}
		x++;
		if(x==groups)
		{
			x=0;
		}
	}
	while(x!=ngroup);
	return 0;
}
void ext2_block_release(unsigned int nblock)
{
	unsigned int ngroup=(nblock-(sb.block_size?0:1))/sb.blocks_per_group;
	static unsigned char buf[4096];
	unsigned int bs=1<<sb.block_size+10;
	if(nblock==0)
	{
		return;
	}
	nblock=(nblock-(sb.block_size?0:1))%sb.blocks_per_group;
	read_block(gt[ngroup].block_bitmap,buf);
	buf[nblock>>3]&=~(1<<(nblock&7));
	write_block(gt[ngroup].block_bitmap,buf);
	gt[ngroup].free_blocks++;
	sb.free_blocks++;
	lock_set8(&sb_changed,1);
}
unsigned int ext2_alloc_ind_blocks(unsigned int ninode,unsigned int level)
{
	unsigned int n,n1;
	static unsigned int buf1[4096];
	unsigned int *buf=buf1+(level-1)*1024;
	if(level)
	{
		n=ext2_block_alloc(ninode);
		if(n==0)
		{
			return 0;
		}
		n1=ext2_alloc_ind_blocks(ninode,level-1);
		if(n1==0)
		{
			ext2_block_release(n);
			return 0;
		}
		memset(buf,0,4096);
		buf[0]=n1;
		write_block(n,buf);
		return n;
	}
	else
	{
		n=ext2_block_alloc(ninode);
		return n;
	}
}
int ext2_append(struct file *fp)
{
	unsigned long long int fsize=file_size(&fp->inode);
	unsigned int nbnew=fsize==0?0:(fsize-1>>sb.block_size+10)+1;
	unsigned int nblock;
	unsigned int size1=1<<8+sb.block_size;
	unsigned int i1,i2,i3;
	unsigned int n1,n2;
	static unsigned int buf[1024];
	if(nbnew<12)
	{
		nblock=ext2_block_alloc(fp->ninode);
		if(nblock==0)
		{
			return 0;
		}
		fp->inode.block[nbnew]=nblock;
		fp->inode.blocks+=2<<sb.block_size;
		return 1;
	}
	nbnew-=12;
	if(nbnew<size1)
	{
		if(!nbnew)
		{
			nblock=ext2_alloc_ind_blocks(fp->ninode,1);
			if(nblock==0)
			{
				return 0;
			}
			fp->inode.block[12]=nblock;
			fp->inode.blocks+=4<<sb.block_size;
			return 1;
		}
		else
		{
			read_block(fp->inode.block[12],buf);
			nblock=ext2_block_alloc(fp->ninode);
			if(nblock==0)
			{
				return 0;
			}
			buf[nbnew]=nblock;
			write_block(fp->inode.block[12],buf);
			fp->inode.blocks+=2<<sb.block_size;
			return 1;
		}
	}
	nbnew-=size1;
	size1<<=8+sb.block_size;
	if(nbnew<size1)
	{
		i1=nbnew>>8+sb.block_size;
		i2=nbnew&(1<<8+sb.block_size)-1;
		if(i2==0)
		{
			if(i1==0)
			{
				nblock=ext2_alloc_ind_blocks(fp->ninode,2);
				if(nblock==0)
				{
					return 0;
				}
				fp->inode.block[13]=nblock;
				fp->inode.blocks+=6<<sb.block_size;
				return 1;
			}
			else
			{
				read_block(fp->inode.block[13],buf);
				nblock=ext2_alloc_ind_blocks(fp->ninode,1);
				if(nblock==0)
				{
					return 0;
				}
				buf[i1]=nblock;
				write_block(fp->inode.block[13],buf);
				fp->inode.blocks+=4<<sb.block_size;
				return 1;
			}
		}
		else
		{
			read_block(fp->inode.block[13],buf);
			n1=buf[i1];
			read_block(n1,buf);
			nblock=ext2_block_alloc(fp->ninode);
			if(nblock==0)
			{
				return 0;
			}
			buf[i2]=nblock;
			write_block(n1,buf);
			fp->inode.blocks+=2<<sb.block_size;
			return 1;
		}
	}
	nbnew-=size1;
	size1<<=8+sb.block_size;
	if(nbnew<size1)
	{
		i1=nbnew>>(8+sb.block_size)*2;
		i2=(nbnew&(1<<(8+sb.block_size)*2)-1)>>8+sb.block_size;
		i3=nbnew&(1<<8+sb.block_size)-1;
		if(i3==0)
		{
			if(i2==0)
			{
				if(i1==0)
				{
					nblock=ext2_alloc_ind_blocks(fp->ninode,3);
					if(nblock==0)
					{
						return 0;
					}
					fp->inode.block[14]=nblock;
					fp->inode.blocks+=8<<sb.block_size;
					return 1;
				}
				else
				{
					read_block(fp->inode.block[14],buf);
					nblock=ext2_alloc_ind_blocks(fp->ninode,2);
					if(nblock==0)
					{
						return 0;
					}
					buf[i1]=nblock;
					write_block(fp->inode.block[14],buf);
					fp->inode.blocks+=6<<sb.block_size;
					return 1;
				}
			}
			else
			{
				read_block(fp->inode.block[14],buf);
				n1=buf[i1];
				read_block(n1,buf);
				nblock=ext2_alloc_ind_blocks(fp->ninode,1);
				if(nblock==0)
				{
					return 0;
				}
				buf[i2]=nblock;
				write_block(n1,buf);
				fp->inode.blocks+=4<<sb.block_size;
				return 1;
			}
		}
		else
		{
			read_block(fp->inode.block[14],buf);
			n1=buf[i1];
			read_block(n1,buf);
			n2=buf[i2];
			read_block(n2,buf);
			nblock=ext2_block_alloc(fp->ninode);
			if(nblock==0)
			{
				return 0;
			}
			buf[i3]=nblock;
			write_block(n2,buf);
			fp->inode.blocks+=2<<sb.block_size;
			return 1;
		}
	}
	else
	{
		return 0;
	}
}
int file_write(struct file *fp,unsigned long long int start,int size,void *ptr)
{
	unsigned long long int fsize=file_size(&fp->inode),new_fsize;
	int b_written=0;
	unsigned int size1,bstart=start>>10+sb.block_size,off=start&(1<<10+sb.block_size)-1;
	static unsigned char buf[4096];
	while(size>0)
	{
		if(size>(1<<sb.block_size+10)-off)
		{
			size1=(1<<sb.block_size+10)-off;
		}
		else
		{
			size1=size;
		}
		new_fsize=((unsigned long long int)bstart<<10+sb.block_size)+off+size1;
		if(file_read_block(fp,bstart,buf)==0)
		{
			if(ext2_append(fp)==0)
			{
				break;
			}
		}
		if(new_fsize>fsize)
		{
			fsize=new_fsize;
			file_size_set(&fp->inode,fsize);
		}
		memcpy(buf+off,ptr,size1);
		if(file_write_block(fp,bstart,buf)==0)
		{
			break;
		}
		bstart++;
		size-=size1;
		b_written+=size1;
		ptr=(char *)ptr+size1;
		off=0;
	}
	return b_written;
}
unsigned int ext2_inode_alloc(unsigned int d_inode)
{
	unsigned int ngroup,x,x1;
	static unsigned char buf[4096];
	if(sb.free_inodes==0)
	{
		return 0;
	}
	if(d_inode==2)
	{
		x=0;
		ngroup=0;
		while(x<groups)
		{
			if(gt[ngroup].free_inodes<gt[x].free_inodes)
			{
				ngroup=x;
			}
			x++;
		}
	}
	else
	{
		ngroup=(d_inode-1)/sb.inodes_per_group;
	}
	x=ngroup;
	do
	{
		if(gt[x].free_inodes)
		{
			x1=0;
			read_block(gt[x].inode_bitmap,buf);
			while(x1<sb.inodes_per_group)
			{
				if(!(buf[x1>>3]&1<<(x1&7)))
				{
					buf[x1>>3]|=1<<(x1&7);
					write_block(gt[x].inode_bitmap,buf);
					gt[x].free_inodes--;
					sb.free_inodes--;
					lock_set8(&sb_changed,1);
					return x1+x*sb.inodes_per_group+1;
				}
				x1++;
			}
		}
		x++;
		if(x==groups)
		{
			x=0;
		}
	}
	while(x!=ngroup);
	return 0;
}
void ext2_inode_release(unsigned int ninode)
{
	unsigned int ngroup=(ninode-1)/sb.inodes_per_group;
	static unsigned char buf[4096];
	struct ext2_inode inode;
	unsigned char isdir=0;
	inode_read(ninode,&inode);
	if((inode.mode&0170000)==040000)
	{
		isdir=1;
	}
	memset(&inode,0,sizeof(inode));
	inode_write(ninode,&inode);
	ninode=(ninode-1)%sb.inodes_per_group;
	read_block(gt[ngroup].inode_bitmap,buf);
	buf[ninode>>3]&=~(1<<(ninode&7));
	write_block(gt[ngroup].inode_bitmap,buf);
	gt[ngroup].free_inodes++;
	if(isdir)
	{
		gt[ngroup].used_dirs--;
	}
	sb.free_inodes++;
	lock_set8(&sb_changed,1);
}
unsigned int ext2_get_dlen(unsigned char *buf,unsigned int off)
{
	struct ext2_dirent *dir=(void *)(buf+off);
	unsigned int off1;
	if(dir->inode)
	{
		off1=off+8+((dir->name_len-1>>2)+1<<2);
	}
	else
	{
		off1=off;
	}
	off+=dir->rec_len;
	dir=(void *)(buf+off);
	while(off<1<<sb.block_size+10&&dir->inode==0)
	{
		off+=dir->rec_len;
		dir=(void *)(buf+off);
	}
	return off-off1;
}
unsigned int ext2_dirent_add(struct file *dir,unsigned int inode,unsigned char type,char *name)
{
	static unsigned char buf[4096];
	unsigned int start=0,x;
	unsigned int bsize=1<<sb.block_size+10,l=strlen(name);
	unsigned int esize=8+((l-1>>2)+1<<2);
	unsigned int vlen,dlen;
	struct ext2_dirent *dirent;
	
	while(file_read_block(dir,start,buf))
	{
		x=0;
		while(x<bsize)
		{
			dirent=(struct ext2_dirent *)(buf+x);
			dlen=ext2_get_dlen(buf,x);
			if(dlen>=esize)
			{
				if(dirent->inode==0)
				{
					esize=dlen;
					goto End;
				}
				else
				{
					vlen=8+((dirent->name_len-1>>2)+1<<2);
					dirent->rec_len=vlen;
					x+=vlen;
					esize=dlen;
					goto End;
				}
			}
			x+=dirent->rec_len;
		}
		start++;
	}
	if(ext2_append(dir)==0)
	{
		return 0;
	}
	file_size_set(&dir->inode,file_size(&dir->inode)+bsize);
	memset(buf,0,4096);
	x=0;
	esize=bsize;
End:
	dirent=(struct ext2_dirent *)(buf+x);
	memset(dirent,0,esize);
	dirent->inode=inode;
	dirent->name_len=l;
	dirent->rec_len=esize;
	memcpy(dirent->name,name,l);
	if(sb.features_incompat&2)
	{
		dirent->file_type=type;
	}
	file_write_block(dir,start,buf);
	return 1;
}
unsigned int file_mknod(struct file *dir,char *name,unsigned short int mode,unsigned int devnum,char *link_path)
{
	unsigned int inode,ngroup;
	unsigned int l=0;
	unsigned char type,dev=0;
	struct ext2_inode i={0};
	if(link_path)
	{
		l=strlen(link_path);
	}
	switch(mode&0170000)
	{
		case 010000:type=5;
		break;
		case 020000:type=3;
		dev=1;
		break;
		case 040000:type=2;
		break;
		case 060000:type=4;
		dev=1;
		break;
		case 0100000:type=1;
		break;
		case 0120000:type=7;
		break;
		case 0140000:type=6;
		break;
		default:return 0;
	}
	if(file_search(dir,name))
	{
		return 0;
	}
	if((inode=ext2_inode_alloc(dir->ninode))==0)
	{
		return 0;
	}
	if(ext2_dirent_add(dir,inode,type,name)==0)
	{
		ext2_inode_release(inode);
		return 0;
	}
	i.mode=mode;
	i.links=1;
	if(dev)
	{
		i.block[0]=devnum;
	}
	else if(type==7&&l<=60)
	{
		i.size=l;
		memcpy(i.block,link_path,l);
	}
	else if(type==2)
	{
		ngroup=(inode-1)/sb.inodes_per_group;
		gt[ngroup].used_dirs++;
		lock_set8(&sb_changed,1);
	}
	inode_write(inode,&i);
	return inode;
}
int file_symlink(char *link_path,struct file *dir,char *name)
{
	unsigned int l=strlen(link_path);
	unsigned int ninode;
	struct file *fp;
	if(ninode=file_mknod(dir,name,0120777,0,link_path))
	{
		if(l>60)
		{
			if((fp=file_load(ninode,FILE_MODE_RW))==NULL)
			{
				ext2_inode_release(ninode);
				return 0;
			}
			file_write(fp,0,l,name);
			file_release(fp);
		}
		return 1;
	}
	return 0;
}
int file_link(struct file *fp,struct file *dir,char *name)
{
	unsigned int ngroup;
	unsigned char type;
	switch(fp->inode.mode&0170000)
	{
		case 010000:type=5;
		break;
		case 020000:type=3;
		break;
		case 040000:type=2;
		break;
		case 060000:type=4;
		break;
		case 0100000:type=1;
		break;
		case 0120000:type=7;
		break;
		case 0140000:type=6;
		break;
	}
	if(ext2_dirent_add(dir,fp->ninode,type,name)==0)
	{
		return 0;
	}
	fp->inode.links++;
	return 1;
}
unsigned int file_mkdir(struct file *dir,char *name)
{
	struct file *fp;
	unsigned int ninode,ngroup;
	static unsigned char buf[4096];
	struct ext2_dirent *dirent;
	struct ext2_inode inode={0};
	if((ninode=ext2_inode_alloc(dir->ninode))==0)
	{
		return 0;
	}
	if((inode.block[0]=ext2_block_alloc(dir->ninode))==0)
	{
		ext2_inode_release(ninode);
		return 0;
	}
	if((ext2_dirent_add(dir,ninode,2,name))==0)
	{
		ext2_block_release(inode.block[0]);
		ext2_inode_release(ninode);
		return 0;
	}
	inode.mode=040755;
	inode.size=1<<sb.block_size+10;
	inode.links=2;
	inode.blocks=2<<sb.block_size;
	dir->inode.links++;
	inode_write(ninode,&inode);
	memset(buf,0,4096);
	dirent=(void *)buf;
	dirent->file_type=2;
	dirent->inode=ninode;
	dirent->rec_len=12;
	dirent->name_len=1;
	dirent->name[0]='.';
	dirent=(void *)(buf+12);
	dirent->file_type=2;
	dirent->inode=dir->ninode;
	dirent->rec_len=(1<<sb.block_size+10)-12;
	dirent->name_len=2;
	dirent->name[0]='.';
	dirent->name[1]='.';
	write_block(inode.block[0],buf);
	ngroup=(ninode-1)/sb.inodes_per_group;
	gt[ngroup].used_dirs++;
	lock_set8(&sb_changed,1);
	return ninode;
}
void release_file_blocks(struct ext2_inode *inode)
{
	unsigned long long int fsize=file_size(inode),size1;
	unsigned int n=0,n1=0,n2=0,l=0;
	static unsigned int buf[3072];
	while(fsize)
	{
		if(fsize>1<<sb.block_size+10)
		{
			size1=1<<sb.block_size+10;
		}
		else
		{
			size1=fsize;
		}
		fsize-=size1;
		switch(l)
		{
			case 0:
			ext2_block_release(inode->block[n]);
			n++;
			if(n==12)
			{
				l=1;
				n=0;
			}
			break;
			case 1:
			if(n==0)
			{
				read_block(inode->block[12],buf);
				ext2_block_release(inode->block[12]);
			}
			ext2_block_release(buf[n]);
			n++;
			if(n==1<<sb.block_size+8)
			{
				n=0;
				l=2;
			}
			break;
			case 2:
			if(n==0)
			{
				if(n1==0)
				{
					read_block(inode->block[13],buf+1024);
					ext2_block_release(inode->block[13]);
				}
				read_block(buf[1024+n1],buf);
				ext2_block_release(buf[1024+n1]);
			}
			ext2_block_release(buf[n]);
			n++;
			if(n==1<<sb.block_size+8)
			{
				n=0;
				n1++;
				if(n1==1<<sb.block_size+8)
				{
					n1=0;
					l=3;
				}
			}
			break;
			case 3:
			if(n==0)
			{
				if(n1==0)
				{
					if(n2==0)
					{
						read_block(inode->block[14],buf+2048);
						ext2_block_release(inode->block[14]);
					}
					read_block(buf[2048+n2],buf+1024);
					ext2_block_release(buf[2048+n2]);
				}
				read_block(buf[1024+n1],buf);
				ext2_block_release(buf[1024+n1]);
			}
			ext2_block_release(buf[n]);
			n++;
			if(n==1<<sb.block_size+8)
			{
				n=0;
				n1++;
				if(n1==1<<sb.block_size+8)
				{
					n1=0;
					n2++;
					if(n2==1<<sb.block_size+8)
					{
						return;
					}
				}
			}
			break;
		}
	}
}
int file_unlink(struct file *dir,char *name)
{
	unsigned int ninode,l=strlen(name);
	unsigned int start=0,x;
	unsigned int bsize=1<<sb.block_size+10;
	unsigned short int type;
	static unsigned char buf[4096];
	struct ext2_dirent *dirent;
	struct ext2_inode inode={0};
	while(file_read_block(dir,start,buf))
	{
		x=0;
		while(x<bsize)
		{
			dirent=(void *)(buf+x);
			if(dirent->inode)
			{
				if(dirent->name_len==l)
				{
					if(!memcmp(dirent->name,name,l))
					{
						ninode=dirent->inode;
						
						goto found;
					}
				}
			}
			x+=dirent->rec_len;
		}
		start++;
	}
	return 0;
found:
	inode_read(ninode,&inode);
	type=inode.mode&0170000;
	if(type==040000)
	{
		return 0;
	}
	dirent->inode=0;
	dirent->name_len=0;
	dirent->file_type=0;
	file_write_block(dir,start,buf);
	if(inode.links!=1)
	{
		inode.links--;
		inode_write(ninode,&inode);
		return 1;
	}
	
	if(type==0100000||type==0120000&&file_size(&inode)>60)
	{
		release_file_blocks(&inode);
	}
	ext2_inode_release(ninode);
	return 1;
}
int file_rmdir(struct file *dir,char *name)
{
	unsigned int ninode,l=strlen(name);
	unsigned int start=0,x,off=0;
	unsigned int bsize=1<<sb.block_size+10;
	unsigned short int type;
	static unsigned char buf[4096],buf1[264];
	struct ext2_dirent *dirent,*dirent1=(void *)buf1;
	struct file *fp;
	struct ext2_inode inode={0};
	while(file_read_block(dir,start,buf))
	{
		x=0;
		while(x<bsize)
		{
			dirent=(void *)(buf+x);
			if(dirent->inode)
			{
				if(dirent->name_len==l)
				{
					if(!memcmp(dirent->name,name,l))
					{
						ninode=dirent->inode;
						
						goto found;
					}
				}
			}
			x+=dirent->rec_len;
		}
		start++;
	}
	return 0;
found:
	fp=file_load(ninode,FILE_MODE_RO);
	if(fp==NULL)
	{
		return 0;
	}
	type=fp->inode.mode&0170000;
	if(type!=040000)
	{
		file_release(fp);
		return 0;
	}
	if(fp->inode.links!=2)
	{
		file_release(fp);
		return 0;
	}
	while(off=file_readdir(fp,off,dirent1))
	{
		if(dirent1->name_len>2||dirent1->name[0]!='.'||dirent1->name_len==2&&dirent1->name[1]!='.')
		{
			file_release(fp);
			return 0;
		}
	}
	memcpy(&inode,&fp->inode,sizeof(inode));
	file_release(fp);
	release_file_blocks(&inode);
	ext2_inode_release(ninode);
	dirent->inode=0;
	dirent->name_len=0;
	dirent->file_type=0;
	file_write_block(dir,start,buf);
	dir->inode.links--;
	return 1;
}
int file_readlink(struct file *fp,char *buf)
{
	if(fp->inode.size<=60)
	{
		memcpy(buf,fp->inode.block,fp->inode.size);
		return fp->inode.size;
	}
	else
	{
		return file_read(fp,0,4095,buf);
	}
}
unsigned int find_dirent(unsigned int dir_inode,char *name)
{
	struct file *fp;
	unsigned int ninode;
	if(fp=file_load(dir_inode,FILE_MODE_RO))
	{
		ninode=file_search(fp,name);
		file_release(fp);
		return ninode;
	}
	return 0;
}
unsigned int find_dirent_by_path(unsigned int cur_dir,char *path)
{
	unsigned int x1=0,x2=0;
	char c,buf[256]={0};
	if(path[0]=='/')
	{
		path++;
		cur_dir=2;
	}
	while(c=path[x2])
	{
		if(c=='/')
		{
			if(x2-x1>255)
			{
				return 0;
			}
			if(x1!=x2)
			{
				memcpy(buf,path+x1,x2-x1);
				buf[x2-x1]=0;
				if((cur_dir=find_dirent(cur_dir,buf))==0)
				{
					return 0;
				}
			}
			x1=x2+1;
		}
		x2++;
	}
	if(x1!=x2)
	{
		cur_dir=find_dirent(cur_dir,path+x1);
	}
	return cur_dir;
}
void ext2_init(void)
{
	LARGE_INTEGER size={0};
	DISK_GEOMETRY_EX size1={0};
	PARTITION_INFORMATION_EX size2={0};
	unsigned int buf;
	unsigned int gdt_size;
	unsigned int x=7;
	hDev=CreateFile(dev_path,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
	if(hDev==INVALID_HANDLE_VALUE)
	{
		printf("Fatal error: Cannot open \"%s\".\n",dev_path);
		err_quit();
	}
	if(DeviceIoControl(hDev,IOCTL_DISK_GET_PARTITION_INFO_EX,NULL,0,&size2,sizeof(size2),&buf,0))
	{
		devsize=size2.PartitionLength.QuadPart;
	}
	else if(DeviceIoControl(hDev,IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,NULL,0,&size1,sizeof(size1),&buf,0))
	{
		devsize=size1.DiskSize.QuadPart;
	}
	else if(GetFileSizeEx(hDev,&size))
	{
		devsize=size.QuadPart;
	}
	else
	{
		printf("Fatal error: Failed to get device size.\n");
		err_quit();
	}
	SetFilePointer(hDev,0,NULL,FILE_BEGIN);
	buf=0;
	ReadFile(hDev,sb_buf,4096,&buf,NULL);
	if(buf!=4096)
	{
		printf("Fatal error: I/O error while loading superblock.\n");
		err_quit();
	}
	memcpy(&sb,sb_buf+1024,1024);
	if(sb.magic!=0xef53||sb.block_size>2)
	{
		printf("Fatal error: Filesystem not recognized.\n");
		err_quit();
	}
	while(x<10+sb.block_size)
	{
		if(sb.inode_size==1<<x)
		{
			goto X1;
		}
		x++;
	}
	printf("Fatal error: Filesystem not recognized.\n");
	err_quit();
X1:
	if(sb.features_incompat&(~2)||sb.features_ro_compat&(~3))
	{
		printf("Fatal error: Filesystem has unsupported features, cannot continue.\n");
		err_quit();
	}
	
	buf=0;
	if(sb.block_size)
	{
		groups=(sb.blocks-1)/sb.blocks_per_group+1;
		SetFilePointer(hDev,1<<sb.block_size+10,NULL,FILE_BEGIN);
	}
	else
	{
		groups=(sb.blocks-2)/sb.blocks_per_group+1;
		SetFilePointer(hDev,2048,NULL,FILE_BEGIN);
	}
	gdt_size=32*groups;
	gdt_size=(gdt_size-1>>sb.block_size+10)+1<<sb.block_size+10;
	if((gt=malloc(gdt_size))==NULL)
	{
		printf("Fatal error: Cannot allocate memory.\n");
		err_quit();
	}
	ReadFile(hDev,gt,gdt_size,&buf,NULL);
	if(buf!=gdt_size)
	{
		printf("Fatal error: I/O error while loading block group descriptors.\n");
		err_quit();
	}
	CreateThread(NULL,0,T_ext2_io,NULL,0,NULL);
}
