#include <stdint.h>

uint16_t inports(uint16_t port) 
{
    uint16_t r;
    asm("inw %1, %0" : "=a" (r) : "dN" (port));
    return r;
}

void outports(uint16_t port, uint16_t data) 
{
    asm("outw %1, %0" : : "dN" (port), "a" (data));
}

uint8_t inportb(uint16_t port) 
{
    uint8_t r;
    asm("inb %1, %0" : "=a" (r) : "dN" (port));
    return r;
}

size_t inportl(uint16_t _port)
{
  size_t rv;
  asm volatile("inl %%dx, %%eax"
               : "=a"(rv)
               : "dN"(_port));
  return rv;
}

void outportl(uint16_t _port, size_t _data)
{
  asm volatile("outl %%eax, %%dx"
               :
               : "dN"(_port), "a"(_data));
}

char inb(short port) {
  char data;
  asm volatile("in %1,%0" : "=a" (data) : "d" (port));
  return data;
}

void outb(short port,char data){
  asm volatile("out %0,%1" : : "a" (data), "d" (port));
}

void outsl(int port, const void *addr, int cnt) {
  asm volatile("cld; rep outsl" :
               "=S" (addr), "=c" (cnt) :
               "d" (port), "0" (addr), "1" (cnt) :
               "cc");
}

void insl(int port, void *addr, int cnt) {
  asm volatile("cld; rep insl" :
               "=D" (addr), "=c" (cnt) :
               "d" (port), "0" (addr), "1" (cnt) :
               "memory", "cc");
}

void loadgs(uint16_t v){
  asm volatile("movw %0, %%gs" : : "r" (v));
}

void stosl(void *addr, int data, int cnt){
  asm volatile("cld; rep stosl" :
                  "=D" (addr), "=c" (cnt) :
                  "0" (addr), "1" (cnt), "a" (data) :
                  "memory", "cc");
}

void stosb(void *addr, int data, int cnt){
  asm volatile("cld; rep stosb" :
                  "=D" (addr), "=c" (cnt) :
                  "0" (addr), "1" (cnt), "a" (data) :
                  "memory", "cc");
}

void iowait(){
  outb(0x80, 0);
}