#ifndef __IO_H__
#define __IO_H__

uint16_t inports(uint16_t port);
void outports(uint16_t port, uint16_t data);
uint8_t inportb(uint16_t port);
size_t inportl(uint16_t _port);
void outportl(uint16_t _port, size_t _data);
char inb(short port);
void outb(short port,char data);
void outsl(int port, const void *addr, int cnt);
void insl(int port, void *addr, int cnt);
void loadgs(uint16_t v);
void stosl(void *addr, int data, int cnt);
void stosb(void *addr, int data, int cnt);
void iowait();

#endif