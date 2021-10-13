void cmd_help(int argc,char **argv);
void cmd_exit(int argc,char **argv)
{
	printf("Synchronizing data, please wait.\n");
	ext2_sync(1);
	exit(0);
}
unsigned int current_dir=2;

struct dirent_list
{
	unsigned int ninode;
	char name[256];
	struct ext2_inode inode;
	struct dirent_list *next;
};
void print_mode(unsigned short int mode)
{
	switch(mode&0170000)
	{
		case 010000:putchar('p');
		break;
		case 020000:putchar('c');
		break;
		case 040000:putchar('d');
		break;
		case 060000:putchar('b');
		break;
		case 0100000:putchar('-');
		break;
		case 0120000:putchar('l');
		break;
		case 0140000:putchar('s');
		break;
	}
	putchar(mode&0400?'r':'-');
	putchar(mode&0200?'w':'-');
	putchar(mode&0100?(mode&04000?'s':'x'):(mode&04000?'S':'-'));
	putchar(mode&040?'r':'-');
	putchar(mode&020?'w':'-');
	putchar(mode&010?(mode&02000?'s':'x'):(mode&02000?'S':'-'));
	putchar(mode&04?'r':'-');
	putchar(mode&02?'w':'-');
	putchar(mode&01?(mode&01000?'t':'x'):(mode&01000?'T':'-'));
}
void cmd_ls(int argc,char **argv)
{
	unsigned int ninode=current_dir;
	unsigned int off=0;
	unsigned char buf[264];
	struct file *fp;
	struct ext2_dirent *dirent=(void *)buf;
	struct dirent_list *head=NULL,*t,*t1,*t2;
	if(argc>1)
	{
		if((ninode=find_dirent_by_path(ninode,argv[1]))==0)
		{
			printf("ls: Cannot open \"%s\".\n",argv[1]);
			return;
		}
	}
	if((fp=file_load(ninode,FILE_MODE_RO))==NULL)
	{
		printf("ls: Cannot open \"%s\".\n",argv[1]);
		return;
	}
	if((fp->inode.mode&0170000)!=040000)
	{
		printf("ls: \"%s\" is not a directory.\n",argv[1]);
		file_release(fp);
		return;
	}
	while(off=file_readdir(fp,off,dirent))
	{
		if((t=malloc(sizeof(*t))))
		{
			t->ninode=dirent->inode;
			inode_read(t->ninode,&t->inode);
			memcpy(t->name,dirent->name,dirent->name_len);
			t->name[dirent->name_len]=0;
			t1=head;
			t2=NULL;
			while(t1&&strcmp(t1->name,t->name)==-1)
			{
				t2=t1;
				t1=t1->next;
			}
			if(t2==NULL)
			{
				t->next=head;
				head=t;
			}
			else
			{
				t2->next=t;
				t->next=t1;
				
			}
		}
	}
	file_release(fp);
	if(head)
	{
		printf("       inode       mode  links                 size       dev    uid    gid name\n");
	}
	while(head)
	{
		t=head;
		head=head->next;
		printf("%12u ",t->ninode);
		print_mode(t->inode.mode);
		printf(" %6u ",t->inode.links);
		printf("%20llu ",file_size(&t->inode));
		switch(t->inode.mode&0170000)
		{
			case 020000:
			case 060000:
			printf("%4u %4u ",t->inode.block[0]>>8,t->inode.block[0]&255);
			break;
			default:
			printf("          ");
		}
		printf("%6u %6u ",t->inode.uid,t->inode.gid);
		printf("%s\n",t->name);
		free(t);
	}
}
void print_progress(unsigned int n,unsigned int speed)
{
	printf("%3u.%02u%% %4u.%03u MB/s\r",n/100,n%100,speed/1024,speed%1024*1000/1024);
}
int do_pull(struct file *fpi,char *path)
{
	static unsigned char buf[65536];
	unsigned char buf1[264];
	unsigned int n;
	FILE *fpo;
	struct file *new_fpi;
	unsigned long long int off=0;
	struct ext2_dirent *dirent=(void *)buf1;
	char *new_path;
	unsigned int l;
	unsigned int t,t1;
	switch(fpi->inode.mode&0170000)
	{
		case 0100000:
		if((fpo=fopen(path,"wb"))==NULL)
		{
			printf("pull: Cannot open \"%s\".\n",path);
			return 1;
		}
		t=clock();
		while(n=file_read(fpi,off,65536,buf))
		{
			fwrite(buf,n,1,fpo);
			off+=n;
			t1=clock();
			print_progress(off*10000/file_size(&fpi->inode),t1==t?0:off*1000/((t1-t)*1024));
		}
		print_progress(10000,t1==t?0:off*1000/((t1-t)*1024));
		putchar('\n');
		fclose(fpo);
		return 0;
		case 040000:
		CreateDirectory(path,NULL);
		while(off=file_readdir(fpi,off,dirent))
		{
			if(dirent->name_len==2&&dirent->name[0]=='.'&&dirent->name[1]=='.')
			{
				continue;
			}
			if(dirent->name_len==1&&dirent->name[0]=='.')
			{
				continue;
			}
			l=strlen(path);
			printf("Copying %*s\n",dirent->name_len,dirent->name);
			if(new_fpi=file_load(dirent->inode,FILE_MODE_RO))
			{
				if(new_path=malloc(l+dirent->name_len+2))
				{
					memcpy(new_path,path,l);
					new_path[l]='\\';
					memcpy(new_path+l+1,dirent->name,dirent->name_len);
					new_path[l+dirent->name_len+1]=0;
					if(do_pull(new_fpi,new_path))
					{
						free(new_path);
						file_release(new_fpi);
						return 1;
					}
					free(new_path);
				}
				file_release(new_fpi);
			}
		}
		return 0;
		default:printf("pull: Inode %u is not a regular file or a directory, ignored.\n",fpi->ninode);
		return 0;
	}
}
void cmd_pull(int argc,char **argv)
{
	if(argc<3)
	{
		printf("pull: Too few arguments\n");
		return;
	}
	unsigned int ninode;
	struct file *fpi;
	
	if((ninode=find_dirent_by_path(current_dir,argv[1]))==0)
	{
		printf("pull: Cannot open \"%s\".\n",argv[1]);
		return;
	}
	if((fpi=file_load(ninode,FILE_MODE_RO))==NULL)
	{
		printf("pull: Cannot open \"%s\".\n",argv[1]);
		return;
	}
	do_pull(fpi,argv[2]);
	file_release(fpi);
}
int do_push(struct file *dir,char *name,char *path)
{
	unsigned long long int off=0;
	static unsigned char buf[65536];
	FILE *fpi;
	struct file *fpo;
	HANDLE hfind;
	WIN32_FIND_DATA fdata;
	unsigned int ninode,n,l;
	char *new_path;
	unsigned long long int fsize;
	unsigned int t,t1;
	if(fpi=fopen(path,"rb"))
	{
		if(ninode=file_mknod(dir,name,0100644,0,NULL))
		{
			if(fpo=file_load(ninode,FILE_MODE_RW))
			{
				fseeko64(fpi,0,SEEK_END);
				fsize=ftello64(fpi);
				fseeko64(fpi,0,SEEK_SET);
				t=clock();
				while(n=fread(buf,1,65536,fpi))
				{
					if(file_write(fpo,off,n,buf)!=n)
					{
						printf("push: \"%s\": Copying did not complete.\n",name);
						break;
					}
					off+=n;
					t1=clock();
					print_progress(off*10000/fsize,t1==t?0:off*1000/((t1-t)*1024));
				}
				file_release(fpo);
				print_progress(10000,t1==t?0:off*1000/((t1-t)*1024));
				putchar('\n');
			}
			else
			{
				printf("push: \"%s\": Copying did not complete.\n",name);
			}
		}
		else
		{
			printf("push: Cannot create file \"%s\".\n",name);
		}
		fclose(fpi);
		return 0;
	}
	l=strlen(path);
	if(new_path=malloc(l+260))
	{
		memcpy(new_path,path,l);
		new_path[l]='\\';
		new_path[l+1]='*';
		new_path[l+2]=0;
		if((hfind=FindFirstFile(new_path,&fdata))!=INVALID_HANDLE_VALUE)
		{
			if(ninode=file_mkdir(dir,name))
			{
				if(fpo=file_load(ninode,FILE_MODE_RW))
				{
					do
					{
						if(!strcmp(fdata.cFileName,".")||!strcmp(fdata.cFileName,".."))
						{
							continue;
						}
						printf("Copying %s\n",fdata.cFileName);
						strcpy(new_path+l+1,fdata.cFileName);
						do_push(fpo,fdata.cFileName,new_path);
					}
					while(FindNextFile(hfind,&fdata));
					file_release(fpo);
				}
				
			}
			else
			{
				printf("push: Cannot create directory \"%s\".\n",name);
			}
			FindClose(hfind);
		}
		else
		{
			printf("push: Cannot open \"%s\".\n",path);
		}
		free(new_path);
	}
	return 0;
}
void cmd_push(int argc,char **argv)
{
	if(argc<3)
	{
		printf("push: Too few arguments\n");
		return;
	}
	unsigned int ninode;
	unsigned int x=strlen(argv[2]);
	struct file *fpo;
	while(x)
	{
		x--;
		if(argv[2][x]=='/')
		{
			if(x)
			{
				argv[2][x]=0;
				ninode=find_dirent_by_path(current_dir,argv[2]);
				x++;
				break;
			}
			else
			{
				ninode=2;
				x++;
				break;
			}
		}
		else if(!x)
		{
			ninode=current_dir;
		}
	}
	if(ninode==0||strlen(argv[2]+x)>255)
	{
		printf("push: Cannot create file\n");
		return;
	}
	if((fpo=file_load(ninode,FILE_MODE_RW))==NULL)
	{
		printf("push: Cannot create file\n");
		return;
	}
	if((fpo->inode.mode&0170000)!=040000)
	{
		printf("push: Cannot create file\n");
		file_release(fpo);
		return;
	}
	do_push(fpo,argv[2]+x,argv[1]);
	file_release(fpo);
}
void cmd_mknod(int argc,char **argv)
{
	unsigned short int mode=0644;
	unsigned int devnum[2]={0};
	unsigned int x;
	unsigned int ninode;
	if(argc<3)
	{
		printf("mknod: Too few arguments\n");
		return;
	}
	x=strlen(argv[1]);
	switch(argv[2][0])
	{
		case 'b':mode|=060000;
		break;
		case 'c':mode|=020000;
		break;
		case 'p':mode|=010000;
		break;
		case 'r':mode|=0100000;
		break;
		case 's':mode|=0140000;
		break;
		default:return;
	}
	if(argc>3)
	{
		sscanf(argv[3],"%u",devnum);
		if(argc>4)
		{
			sscanf(argv[4],"%u",devnum+1);
		}
	}
	struct file *fp;
	while(x)
	{
		x--;
		if(argv[1][x]=='/')
		{
			if(x)
			{
				argv[1][x]=0;
				ninode=find_dirent_by_path(current_dir,argv[1]);
				x++;
				break;
			}
			else
			{
				ninode=2;
				x++;
				break;
			}
		}
		else if(!x)
		{
			ninode=current_dir;
		}
	}
	if(ninode==0||(fp=file_load(ninode,FILE_MODE_RW))==NULL)
	{
		printf("mknod: Cannot create file.\n");
		return;
	}
	if((fp->inode.mode&0170000)!=040000||file_mknod(fp,argv[1]+x,mode,devnum[0]<<8|devnum[1],NULL)==0)
	{
		printf("mknod: Cannot create file.\n");
		file_release(fp);
		return;
	}
	file_release(fp);
}
void cmd_mkdir(int argc,char **argv)
{
	unsigned int x;
	unsigned int ninode;
	if(argc<2)
	{
		printf("mkdir: Too few arguments\n");
		return;
	}
	struct file *fp;
	x=strlen(argv[1]);
	while(x)
	{
		x--;
		if(argv[1][x]=='/')
		{
			if(x)
			{
				argv[1][x]=0;
				ninode=find_dirent_by_path(current_dir,argv[1]);
				x++;
				break;
			}
			else
			{
				ninode=2;
				x++;
				break;
			}
		}
		else if(!x)
		{
			ninode=current_dir;
		}
	}
	if(ninode==0||(fp=file_load(ninode,FILE_MODE_RW))==NULL)
	{
		printf("mkdir: Cannot create directory.\n");
		return;
	}
	if((fp->inode.mode&0170000)!=040000||file_mkdir(fp,argv[1]+x)==0)
	{
		printf("mkdir: Cannot create directory.\n");
		file_release(fp);
		return;
	}
	file_release(fp);
}
void cmd_unlink(int argc,char **argv)
{
	unsigned int x;
	unsigned int ninode;
	if(argc<2)
	{
		printf("unlink: Too few arguments\n");
		return;
	}
	x=strlen(argv[1]);
	struct file *fp;
	while(x)
	{
		x--;
		if(argv[1][x]=='/')
		{
			if(x)
			{
				argv[1][x]=0;
				ninode=find_dirent_by_path(current_dir,argv[1]);
				x++;
				break;
			}
			else
			{
				ninode=2;
				x++;
				break;
			}
		}
		else if(!x)
		{
			ninode=current_dir;
		}
	}
	if(ninode==0||(fp=file_load(ninode,FILE_MODE_RW))==NULL)
	{
		printf("unlink: Cannot remove file.\n");
		return;
	}
	if((fp->inode.mode&0170000)!=040000||file_unlink(fp,argv[1]+x)==0)
	{
		printf("unlink: Cannot remove file.\n");
		file_release(fp);
		return;
	}
	file_release(fp);
}
void cmd_rmdir(int argc,char **argv)
{
	unsigned int x;
	unsigned int ninode;
	if(argc<2)
	{
		printf("rmdir: Too few arguments\n");
		return;
	}
	x=strlen(argv[1]);
	struct file *fp;
	if(find_dirent_by_path(current_dir,argv[1])==current_dir)
	{
		printf("rmdir: Cannot remove directory.\n");
		return;
	}
	while(x)
	{
		x--;
		if(argv[1][x]=='/')
		{
			if(x)
			{
				argv[1][x]=0;
				ninode=find_dirent_by_path(current_dir,argv[1]);
				x++;
				break;
			}
			else
			{
				ninode=2;
				x++;
				break;
			}
		}
		else if(!x)
		{
			ninode=current_dir;
		}
	}
	
	if(ninode==0||(fp=file_load(ninode,FILE_MODE_RW))==NULL)
	{
		printf("rmdir: Cannot remove directory.\n");
		return;
	}
	if((fp->inode.mode&0170000)!=040000||file_rmdir(fp,argv[1]+x)==0)
	{
		printf("rmdir: Cannot remove directory.\n");
		file_release(fp);
		return;
	}
	file_release(fp);
}
int do_removeall(struct file *dir,char *name)
{
	unsigned int ninode;
	struct file *fp;
	unsigned char buf[264];
	struct ext2_dirent *dirent=(void *)buf;
	unsigned int off=0;
	char new_name[256];
	
	if(ninode=file_search(dir,name))
	{
		if(ninode==current_dir)
		{
			printf("removeall: Cannot remove \"%s\".\n",name);
			return 1;
		}
		if(fp=file_load(ninode,FILE_MODE_RW))
		{
			if((fp->inode.mode&0170000)==040000)
			{
				while(off=file_readdir(fp,off,dirent))
				{
					memcpy(new_name,dirent->name,dirent->name_len);
					new_name[dirent->name_len]=0;
					if(!strcmp(new_name,".")||!strcmp(new_name,".."))
					{
						continue;
					}
					if(do_removeall(fp,new_name))
					{
						file_release(fp);
						return 1;
					}
				}
				file_release(fp);
				if(!file_rmdir(dir,name))
				{
					printf("removeall: Cannot remove \"%s\".\n",name);
					return 1;
				}
			}
			else
			{
				file_release(fp);
				if(!file_unlink(dir,name))
				{
					printf("removeall: Cannot remove \"%s\".\n",name);
					return 1;
				}
			}
		}
		else
		{
			printf("removeall: Cannot remove \"%s\".\n",name);
			return 1;
		}
	}
	else
	{
		printf("removeall: Cannot remove \"%s\".\n",name);
		return 1;
	}
	return 0;
}
void cmd_removeall(int argc,char **argv)
{
	unsigned int x;
	unsigned int ninode;
	if(argc<2)
	{
		printf("removeall: Too few arguments\n");
		return;
	}
	x=strlen(argv[1]);
	struct file *fp;
	while(x)
	{
		x--;
		if(argv[1][x]=='/')
		{
			if(x)
			{
				argv[1][x]=0;
				ninode=find_dirent_by_path(current_dir,argv[1]);
				x++;
				break;
			}
			else
			{
				ninode=2;
				x++;
				break;
			}
		}
		else if(!x)
		{
			ninode=current_dir;
		}
	}
	
	if(ninode==0||(fp=file_load(ninode,FILE_MODE_RW))==NULL)
	{
		printf("removeall: Cannot remove file.\n");
		return;
	}
	if((fp->inode.mode&0170000)!=040000)
	{
		printf("removeall: Cannot remove file.\n");
		file_release(fp);
		return;
	}
	do_removeall(fp,argv[1]+x);
	file_release(fp);
}
void cmd_symlink(int argc,char **argv)
{
	unsigned int x;
	unsigned int ninode;
	if(argc<3)
	{
		printf("symlink: Too few arguments\n");
		return;
	}
	x=strlen(argv[2]);
	struct file *fp;
	while(x)
	{
		x--;
		if(argv[2][x]=='/')
		{
			if(x)
			{
				argv[2][x]=0;
				ninode=find_dirent_by_path(current_dir,argv[2]);
				x++;
				break;
			}
			else
			{
				ninode=2;
				x++;
				break;
			}
		}
		else if(!x)
		{
			ninode=current_dir;
		}
	}
	
	if(ninode==0||(fp=file_load(ninode,FILE_MODE_RW))==NULL)
	{
		printf("symlink: Cannot create link.\n");
		return;
	}
	if((fp->inode.mode&0170000)!=040000||strlen(argv[1])>4095||file_symlink(argv[1],fp,argv[2]+x)==0)
	{
		printf("symlink: Cannot create link.\n");
		file_release(fp);
		return;
	}
	file_release(fp);
}
void cmd_readlink(int argc,char **argv)
{
	if(argc<2)
	{
		printf("readlink: Too few arguments\n");
		return;
	}
	struct file *fp;
	unsigned int ninode=find_dirent_by_path(current_dir,argv[1]);
	static unsigned char buf[4096];
	unsigned int n;
	if(ninode==0)
	{
		printf("readlink: Cannot open link.\n");
		return;
	}
	if((fp=file_load(ninode,FILE_MODE_RO))==NULL)
	{
		printf("readlink: Cannot open link.\n");
		return;
	}
	if((fp->inode.mode&0170000)!=0120000)
	{
		printf("readlink: Not a symbolic link.\n");
		file_release(fp);
		return;
	}
	n=file_readlink(fp,buf);
	buf[n]=0;
	file_release(fp);
	printf("%s\n",buf);
}
void cmd_link(int argc,char **argv)
{
	unsigned int x;
	unsigned int ninode,old_inode;
	if(argc<3)
	{
		printf("link: Too few arguments\n");
		return;
	}
	x=strlen(argv[2]);
	struct file *fp,*old_fp;
	while(x)
	{
		x--;
		if(argv[2][x]=='/')
		{
			if(x)
			{
				argv[2][x]=0;
				ninode=find_dirent_by_path(current_dir,argv[2]);
				x++;
				break;
			}
			else
			{
				ninode=2;
				x++;
				break;
			}
		}
		else if(!x)
		{
			ninode=current_dir;
		}
	}
	if(ninode==0||(fp=file_load(ninode,FILE_MODE_RW))==NULL)
	{
		printf("link: Cannot create hard link.\n");
		return;
	}
	if((fp->inode.mode&0170000)!=040000)
	{
		printf("link: Cannot create hard link.\n");
		file_release(fp);
		return;
	}
	old_inode=find_dirent_by_path(current_dir,argv[1]);
	if(old_inode==0)
	{
		printf("link: Cannot create hard link.\n");
		file_release(fp);
		return;
	}
	if((old_fp=file_load(old_inode,FILE_MODE_RW))==0)
	{
		printf("link: Cannot create hard link.\n");
		file_release(fp);
		return;
	}
	if((old_fp->inode.mode&0170000)==040000)
	{
		printf("link: \"%s\" is a directory.\n",argv[1]);
		file_release(fp);
		file_release(old_fp);
		return;
	}
	if(!file_link(old_fp,fp,argv[2]+x))
	{
		printf("link: Cannot create hard link.\n");
		file_release(fp);
		file_release(old_fp);
		return;
	}
	file_release(fp);
	file_release(old_fp);
}
void cmd_cd(int argc,char **argv)
{
	if(argc<2)
	{
		printf("cd: Too few arguments\n");
		return;
	}
	unsigned int ninode=find_dirent_by_path(current_dir,argv[1]);
	struct ext2_inode inode;
	if(ninode==0)
	{
		printf("cd: Target does not exist.\n");
		return;
	}
	inode_read(ninode,&inode);
	if((inode.mode&0170000)!=040000)
	{
		printf("cd: Target is not a directory.\n");
		return;
	}
	current_dir=ninode;
}
struct pwd_name
{
	char name[256];
	struct pwd_name *next;
};
unsigned int get_dir_name(unsigned int ninode,char *name)
{
	struct file *fp;
	unsigned int parent_ino;
	unsigned char buf[264];
	unsigned int off=0;
	struct ext2_dirent *dirent=(void *)buf;
	parent_ino=find_dirent(ninode,"..");
	if((fp=file_load(parent_ino,FILE_MODE_RO))==NULL)
	{
		return 0;
	}
	while(off=file_readdir(fp,off,dirent))
	{
		if(dirent->inode==ninode)
		{
			memcpy(name,dirent->name,dirent->name_len);
			name[dirent->name_len]=0;
			file_release(fp);
			return parent_ino;
		}
	}
	file_release(fp);
	return 0;
}
void cmd_pwd(int argc,char **argv)
{
	unsigned int ninode=current_dir;
	struct pwd_name *head=NULL,*t;
	
	while(ninode!=2)
	{
		if((t=malloc(sizeof(*t)))==NULL)
		{
			goto Err;
		}
		ninode=get_dir_name(ninode,t->name);
		t->next=head;
		head=t;
		if(ninode==0)
		{
			goto Err;
		}
	}
	if(!head)
	{
		printf("/");
	}
	else
	{
		while(head)
		{
			t=head;
			head=head->next;
			printf("/%s",t->name);
			free(t);
		}
	}
	putchar('\n');
	return;
Err:
	while(head)
	{
		t=head;
		head=head->next;
		free(t);
	}
	printf("pwd: Error while probing CWD.\n");
}
void cmd_chmod(int argc,char **argv)
{
	if(argc<3)
	{
		printf("chmod: Too few arguments\n");
		return;
	}
	unsigned int ninode=find_dirent_by_path(current_dir,argv[1]);
	struct ext2_inode inode;
	unsigned short int mode=0;
	if(sscanf(argv[2],"%o",&mode)<0||mode&0170000)
	{
		printf("chmod: Invalid mode %s.\n",argv[2]);
		return;
	}
	if(ninode==0)
	{
		printf("chmod: \"%s\" does not exist.\n",argv[1]);
		return;
	}
	inode_read(ninode,&inode);
	inode.mode&=0170000;
	inode.mode|=mode;
	inode_write(ninode,&inode);
}
void cmd_chown(int argc,char **argv)
{
	if(argc<4)
	{
		printf("chown: Too few arguments\n");
		return;
	}
	unsigned int ninode=find_dirent_by_path(current_dir,argv[1]);
	struct ext2_inode inode;
	unsigned int uid,gid;
	if(ninode==0)
	{
		printf("chown: \"%s\" does not exist.\n",argv[1]);
		return;
	}
	
	if(sscanf(argv[2],"%u",&uid)<0||uid>65535)
	{
		printf("chown: Invalid UID %s.\n",argv[2]);
		return;
	}
	if(sscanf(argv[3],"%u",&gid)<0||gid>65535)
	{
		printf("chown: Invalid GID %s.\n",argv[3]);
		return;
	}
	inode_read(ninode,&inode);
	inode.uid=uid;
	inode.gid=gid;
	inode_write(ninode,&inode);
}
struct _cmd_handler
{
	char *name;
	char *usage;
	char *format;
	void (*handler)(int,char **);
} cmd_handler[]={
{"cd","Change working directory.","cd [path] -- Change working directory.\n",cmd_cd},
{"chmod","Change file permissions.","chmod [file] [mode] -- Set file permissions.\nmode can only be an octal number.\n",cmd_chmod},
{"chown","Change file owner and group.","chown [file] [uid] [gid] -- Set file owner ID and group ID.\n",cmd_chown},
{"exit","Exit from EXT2FS.","exit -- Exit from EXT2FS.\n",cmd_exit},
{"help","Show commands.","help -- Show list of commands\nhelp [command] -- Show command usage.\n",cmd_help},
{"link","Create hard link.","link [oldname] [newname].\n",cmd_link},
{"ls","List files in a directory.","ls -- List files in current directory\nls [path] -- List files in given path.\n",cmd_ls},
{"mkdir","Create directory.","mkdir [path] -- Create directory.\n",cmd_mkdir},
{"mknod","Create file.","mknod [path] [type] [major dev] [minor dev] -- Create file.\nfollowing types are available:\nb -- block device\nc -- character device\np -- pipe\nr -- regular file\ns -- socket\n",cmd_mknod},
{"pull","Copy a file to local disk.","pull [src] [dst] -- Copy src to dst.\n",cmd_pull},
{"push","Copy a file to ext2 filesystem.","push [src] [dst] -- Copy src to dst.\n",cmd_push},
{"pwd","Print current path.","pwd -- Print current path.\n",cmd_pwd},
{"readlink","Print link target.","readlink [path] -- Print link target.\n",cmd_readlink},
{"removeall","Remove a file or a directory (AND its contents).","removeall [path] -- Remove a file or a directory (AND its contents).\n",cmd_removeall},
{"rmdir","Remove an empty directory.","rmdir [path] -- Remove an empty directory.\n",cmd_rmdir},
{"symlink","Create symbolic link.","symlink [target] [path] -- Create symbolic link of target.\n",cmd_symlink},
{"unlink","Remove a file.","unlink [path] -- Remove a file (NOT directory).\n",cmd_unlink},
0};
void cmd_help(int argc,char **argv)
{
	int x=0;
	char *name;
	if(argc==1)
	{
		while(name=cmd_handler[x].name)
		{
			printf("%s: %s\n",name,cmd_handler[x].usage);
			x++;
		}
		printf("\nFor more information on a specific command, type \"help [command]\".\n");
	}
	else
	{
		while(name=cmd_handler[x].name)
		{
			if(!strcmp(name,argv[1]))
			{
				printf("%s: %s\n%s",name,cmd_handler[x].usage,cmd_handler[x].format);
				return;
			}
			x++;
		}
		printf("help: \"%s\": Unknown command.\n",argv[1]);
	}
}
char cmdline_buf[65536];
char cmdline_buf2[65536];
char *cmdline_ptrs[16];
void cmd_run(void)
{
	int x=0,x1=0,x2=0,y=0;
	char c,*arg0;
	while(x<65535)
	{
		if((c=getchar())==10)
		{
			cmdline_buf[x]=0;
			goto X1;
		}
		cmdline_buf[x]=c;
		x++;
	}
	while(getchar()!=10);
X1:
	cmdline_ptrs[0]=cmdline_buf2;
	while(x2<x&&y<16)
	{
		c=cmdline_buf[x2];
		if(c=='\\')
		{
			cmdline_buf2[x1]=cmdline_buf[x2+1];
			x1++;
			x2+=2;
		}
		else if(c==32)
		{
			while(c==32)
			{
				x2++;
				c=cmdline_buf[x2];
			}
			cmdline_buf2[x1]=0;
			x1++;
			y++;
			cmdline_ptrs[y]=cmdline_buf2+x1;
		}
		else
		{
			cmdline_buf2[x1]=c;
			x1++;
			x2++;
		}
	}
	if(x1)
	{
		cmdline_buf2[x1]=0;
		y++;
	}
	if(y==0)
	{
		return;
	}
	x=0;
	while(arg0=cmd_handler[x].name)
	{
		if(!strcmp(arg0,cmdline_ptrs[0]))
		{
			cmd_handler[x].handler(y,cmdline_ptrs);
			return;
		}
		x++;
	}
	printf("Unknown command \"%s\".\n",cmdline_ptrs[0]);
}

