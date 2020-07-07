# 1 "/home/paul/git/OpenGarage-Firmware/OpenGarage/OpenGarage.ino"
# 1 "/home/paul/git/OpenGarage-Firmware/OpenGarage/OpenGarage.ino"
/* OpenGarage Firmware
 *
 * Main loop wrapper for Arduino
 * Mar 2016 @ OpenGarage.io
 *
 * This file is part of the OpenGarage library
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 */
# 23 "/home/paul/git/OpenGarage-Firmware/OpenGarage/OpenGarage.ino" 2
# 24 "/home/paul/git/OpenGarage-Firmware/OpenGarage/OpenGarage.ino" 2
# 25 "/home/paul/git/OpenGarage-Firmware/OpenGarage/OpenGarage.ino" 2
# 26 "/home/paul/git/OpenGarage-Firmware/OpenGarage/OpenGarage.ino" 2
# 27 "/home/paul/git/OpenGarage-Firmware/OpenGarage/OpenGarage.ino" 2
# 28 "/home/paul/git/OpenGarage-Firmware/OpenGarage/OpenGarage.ino" 2
# 29 "/home/paul/git/OpenGarage-Firmware/OpenGarage/OpenGarage.ino" 2
# 30 "/home/paul/git/OpenGarage-Firmware/OpenGarage/OpenGarage.ino" 2
# 31 "/home/paul/git/OpenGarage-Firmware/OpenGarage/OpenGarage.ino" 2

struct tcp_pcb;
extern struct tcp_pcb* tcp_tw_pcbs;
extern "C" void tcp_abort (struct tcp_pcb* pcb);

void tcpCleanup() // losing bytes work around
{ while(tcp_tw_pcbs)
  { tcp_abort(tcp_tw_pcbs); }}
void do_setup();
void do_loop();

void setup() {
    do_setup();
}

void loop() {
  do_loop();
  tcpCleanup();
}
