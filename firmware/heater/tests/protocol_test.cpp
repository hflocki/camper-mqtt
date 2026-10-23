#define private public
#include "inetbox.h"
#undef private
#include "logger.h"
#include <cassert>
#include <cstdarg>
#include <vector>
using namespace truma;
uint32_t g_us = 1000000;
std::vector<uint8_t> g_tx;
int uart_write_bytes(uart_port_t, const char*d, size_t n){ g_tx.assign((const uint8_t*)d,(const uint8_t*)d+n); return n; }
void log_write(LogLevel, const char *tag, const char *fmt, ...){ va_list a; va_start(a,fmt); printf("  [%s] ",tag); vprintf(fmt,a); printf("\n"); va_end(a);} 

InetBox box;
static uint8_t pidp(uint8_t id){ return id | (addr_parity(id)<<6); }
static void feed(uint8_t b){ box.on_byte_(b); box.last_byte_us_ = micros(); g_us += 1100; }
// Master header; returns slave answer bytes (if any)
static std::vector<uint8_t> header(uint8_t id){
  g_us += 20000; g_tx.clear();
  feed(0x00); feed(0x55); feed(pidp(id));
  auto ans = g_tx;
  for (auto b: ans) feed(b);          // transceiver echo
  if (ans.empty()) g_us += 20000;      // nobody answered -> timeout
  return ans;
}
static void master_frame(uint8_t id, const uint8_t *d){
  g_us += 20000; feed(0x00); feed(0x55); feed(pidp(id));
  for(int i=0;i<8;i++) feed(d[i]);
  feed(id==0x3C? data_checksum(d,8,0) : data_checksum(d,8,id));
}
static void send_multi(const std::vector<uint8_t>& m, uint8_t nad){
  uint8_t f[8]={nad,(uint8_t)(0x10|((m.size()>>8)&0xF)),(uint8_t)(m.size()&0xFF),0,0,0,0,0};
  size_t p=0; for(int i=3;i<8;i++) f[i]= p<m.size()?m[p++]:0xFF;
  master_frame(0x3C,f);
  uint8_t c=1; while(p<m.size()){ uint8_t g[8]={nad,(uint8_t)(0x20|(c&0xF)),0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}; for(int i=2;i<8&&p<m.size();i++) g[i]=m[p++]; master_frame(0x3C,g); c++; }
}
static std::vector<uint8_t> read_multi_response(){
  std::vector<uint8_t> out; auto a = header(0x3D); assert(a.size()==9);
  assert(a[8]==data_checksum(a.data(),8,0));
  if((a[1]&0xF0)==0x00){ for(int i=2;i<2+a[1];i++) out.push_back(a[i]); return out; }
  size_t len=((a[1]&0xF)<<8)|a[2]; for(int i=3;i<8;i++) out.push_back(a[i]);
  while(out.size()<len){ a=header(0x3D); assert(a.size()==9); for(int i=2;i<8&&out.size()<len;i++) out.push_back(a[i]); }
  return out;
}
static std::vector<uint8_t> hex(const char*s){ std::vector<uint8_t> v; unsigned x; while(sscanf(s,"%2x",&x)==1){ v.push_back(x); s+=2; if(*s=='.')s++; else break;} return v; }
static void fill(std::vector<uint8_t> v){ // fix checksum after edits
  v.resize(41,0); v[16]=0; v[16]=data_checksum(&v[10],31,0); send_multi(std::vector<uint8_t>(v.begin(), v.end()), box.node_address_);
  auto ack = read_multi_response(); assert(ack.size()==1 && ack[0]==0xFB);
}
int main(){ setvbuf(stdout,nullptr,_IONBF,0);
  box.cmd_queue_ = xQueueCreate(8,sizeof(Command)); box.time_per_first_byte_us_ = 5729;
  // 1. alive poll before init: we want to be read (0xFF)
  auto a = header(0x18); assert(a.size()==9 && a[0]==0xFF && a[8]==data_checksum(a.data(),8,pidp(0x18)));
  // 2. read by identifier (broadcast) -> product id
  uint8_t rbi[8]={0x7F,0x06,0xB2,0x00,0x17,0x46,0x00,0x1F}; master_frame(0x3C,rbi);
  auto r = header(0x3D); printf("r(%zu):",r.size()); for(auto x:r)printf(" %02X",x); printf(" frames %u crc %u\n",box.frames_ok_,box.crc_errors_); assert(r.size()==9 && r[0]==0x03 && r[2]==0xF2 && r[3]==0x17 && r[6]==0x1F && r[7]==0x01);
  // 3. assign NAD 0x05
  uint8_t an[8]={0x7F,0x06,0xB0,0x17,0x46,0x00,0x1F,0x05}; master_frame(0x3C,an);
  r = header(0x3D); assert(r[0]==0x03 && r[2]==0xF0); assert(box.node_address_==0x05);
  // 4. CP Plus reads state buffer -> init request
  send_multi(hex("BA.00.1F.00.1E.00.00.22.FF.FF.FF"), 0x05);
  auto init = read_multi_response(); assert(init.size()==17 && init[0]==0xFA && init[11]=='T' && init[14]==0x0A);
  // 5. devices + heater status
  auto d0=hex("BB.00.1F.00.1E.00.00.22.FF.FF.FF.54.01.0C.0B.00.79.02.00.01.00.50.00.00.04.03.02.AD.10"); fill(d0);
  auto d1=hex("BB.00.1F.00.1E.00.00.22.FF.FF.FF.54.01.0C.0B.00.27.02.01.01.00.40.03.22.06.00.01.00.00"); fill(d1);
  assert(box.init_received_us_!=0 && box.heater_device_==Device::HEATER_COMBI6D);
  auto ht=hex("BB.00.1F.00.1E.00.00.22.FF.FF.FF.54.01.14.33.00.12.00.00.00.00.00.00.00.00.00.00.01.01.CC.0B.6C.0B.00.00.00.00"); fill(ht);
  assert(box.heater_valid_); printf("room %.1f C water %.1f C\n", temp_code_to_decimal(box.heater_.current_temp_room), temp_code_to_decimal(box.heater_.current_temp_water));
  // 6. nothing pending -> 0xFE
  a = header(0x18); assert(a[0]==0xFE);
  // 7. command: room 21 C, fan high
  Command c; c.type=CommandType::SET_ROOM; c.value=21; c.mode=HeatingMode::MODE_HIGH; box.process_command_(c);
  a = header(0x18); assert(a[0]==0xFF);
  send_multi(hex("BA.00.1F.00.1E.00.00.22.FF.FF.FF"), 0x05);
  auto up = read_multi_response();
  printf("update frame len %zu type %02X\n", up.size(), up[14]);
  assert(up.size()==17+12 && up[14]==0x32);
  std::vector<uint8_t> pad(up); pad.resize(41,0); assert(data_checksum(&pad[10],31,0xFF-pad[16])==pad[16]);
  uint16_t t = up[17]|(up[18]<<8), m = up[19]|(up[20]<<8);
  printf("target %u (%.0f C) mode 0x%X energy %u\n", t, temp_code_to_decimal(t), m, up[27]);
  assert(t==2940 && m==0xA && up[27]==1);
  // 8. timer status (off, 07:00-08:00), then set timer 22:00-06:30, 20 C, HIGH
  auto tm=hex("BB.00.1F.00.1E.00.00.22.FF.FF.FF.54.01.18.3D.00.00.00.00.00.00.00.00.00.00.00.01.01.00.00.00.00.00.00.00.00.00.00.07.00.08"); fill(tm);
  assert(box.timer_valid_ && box.timer_.timer_start_hours==7);
  Command tc; tc.type=CommandType::SET_TIMER; tc.start=22*60; tc.stop=6*60+30; tc.value=20; tc.mode=HeatingMode::MODE_HIGH;
  box.process_command_(tc);
  a = header(0x18); assert(a[0]==0xFF);
  send_multi(hex("BA.00.1F.00.1E.00.00.22.FF.FF.FF"), 0x05);
  auto tu = read_multi_response();
  printf("timer frame len %zu type %02X:", tu.size(), tu[14]); for(size_t i=17;i<tu.size();i++)printf(" %02X",tu[i]); printf("\n");
  assert(tu.size()==17+18 && tu[13]==18 && tu[14]==0x3C && tu[34]==0);
  pad.assign(tu.begin(), tu.end()); pad.resize(41,0); assert(data_checksum(&pad[10],31,0xFF-pad[16])==pad[16]);
  assert((tu[17]|(tu[18]<<8))==2930 && tu[19]==0xA && tu[27]==1 && tu[29]==1 && tu[30]==0 && tu[31]==22 && tu[32]==30 && tu[33]==6);
  // timer off
  Command toff; toff.type=CommandType::TIMER_OFF; box.process_command_(toff);
  send_multi(hex("BA.00.1F.00.1E.00.00.22.FF.FF.FF"), 0x05);
  tu = read_multi_response(); assert(tu[14]==0x3C && tu[29]==0 && tu[31]==22);
  // 9. ignore header nobody answers, then continue
  header(0x20); a = header(0x18); assert(a.size()==9);
  printf("frames ok %u crc errors %u\nALL TESTS PASSED\n", box.frames_ok_, box.crc_errors_);
}
