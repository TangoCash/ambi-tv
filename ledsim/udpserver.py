# -*- coding: utf-8 -*-

import socketserver, socket
from threading import Thread, Lock

class UDPServer(Thread):
	update_func = None
	running = False
	_standby = False
	_lock = Lock()

	# ======================================================
	class UDPHandler(socketserver.BaseRequestHandler):

		# ------------------------------------------------------
		def handle(self):
			if UDPServer.running:
				data = self.request[0]
				if data[0] == 4:
					data = data[4:]
					with UDPServer._lock:
						if UDPServer.update_func is not None and len(data) >= 3:
							led_data = []
							for n in range(0,len(data),3):
								led_data.append( (data[n], data[n+1], data[n+2]) )
							UDPServer.update_func(led_data)

	# ======================================================

	# ------------------------------------------------------
	def __init__(self, upd_func=None, col_func=None, HOST='0.0.0.0', PORT=21324):
		Thread.__init__(self)
		UDPServer.update_func = upd_func
		UDPServer.color_func = col_func
		self.server = socketserver.UDPServer((HOST, int(PORT)), UDPServer.UDPHandler, False)
		self.server.allow_reuse_address = True
		self.server.socket.settimeout(3)
		self.server.server_bind()
		print("udp server bind on %s %i" %(HOST,PORT) )

	# ------------------------------------------------------
	def __del__(self):
		self.stop()

	def standby(self,enable):
		try:
			if enable: UDPServer._lock.acquire(True,3)
			else     : UDPServer._lock.release()
		except:
			print("locking error")
	# ------------------------------------------------------
	def run(self):
		UDPServer.running = True
		self.server.server_activate()
		self.server.serve_forever()

	# ------------------------------------------------------
	def stop(self):
		UDPServer.running = False
		self.server.shutdown()
		self.server.server_close()
