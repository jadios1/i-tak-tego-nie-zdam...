pamietaj o tym cnie:

int len = recvfrom(socketfd,buff,MAX_MSG_LEN,0,&client_fd,&client_len);
if (len <= 0)continue;
buff[len] = '\0';
