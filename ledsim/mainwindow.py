# -*- coding: utf-8 -*-

import re, time, signal, os
import codecs
import tkinter as tk

import tkinter.messagebox as tkMessageBox
import tkinter.filedialog as tkFileDialog

from udpserver import *
#from vidcap import *
#from PIL import Image, ImageTk

class StatusBar(tk.Frame):
	def __init__(self, parent):
		tk.Frame.__init__(self, parent)
		self.text=tk.StringVar()
		self.label=tk.Label(self, bd=1, relief=tk.SUNKEN, anchor=tk.W, textvariable=self.text, padx=2, pady=2)
		self.text.set('')
		self.label.pack(fill=tk.X, side=tk.LEFT,expand=1, padx=2, pady=2)
		self.pack(fill=tk.X, side=tk.LEFT,expand=1, padx=1, pady=1)

	def setText(self,text=''):
		self.text.set(text)

class MainWindow(tk.Frame):

	# ------------------------------------------------------
	def __init__(self, parent=None):
		self.parent = parent
		self.draw_type = 'rect'
		self.led_size = 15
		self.clut = [256*[0],256*[0],256*[0]]
		self.gamma = 1.3
		self.whitepoint = (1.0,1.0,1.0)
		self.canvas = None
		self.UDPport = 21324

		self.leds_t = 64
		self.leds_b = 64
		self.leds_l = 36
		self.leds_r = 36
		self.leds_s = 64

		self.show_numbers = True
		#self.show_video = False
		self.calculateCLUTs()
		self.resetVars()
		self.initUI()
		#if self.show_video:
		#	self.vid = MyVideoCapture(0)

		self.udpServer = UDPServer(self.updateLeds,self.setColorCorrection,PORT=self.UDPport)
		self.udpServer.start()

		self.led_data = len(self.led_widgets) * [(100,200,100)]
		self.updateLeds( self.led_data )

	# ------------------------------------------------------
	def resetUI(self):
		self.udpServer.standby(True)
		try:
			if self.canvas is not None:
				self.canvas.destroy()
			self.led_widgets = []
			self.initCanvas()
		finally:
			self.udpServer.standby(False)

	# ------------------------------------------------------
	def resetVars(self):
		self.win_width = 1920
		self.win_height = 1080
		self.led_rects = []
		self.led_widgets = []

		self.read_cfg()

		# top
		led_w = int((self.win_width-2*self.leds_s) / self.leds_t)
		for i in range(self.leds_t):
			self.led_rects.append([
				int(i*led_w)+self.leds_s,
				0,
				int(i*led_w)+self.leds_s+led_w,
				self.leds_s
			])

		# right
		led_h = int((self.win_height-2*self.leds_s) / self.leds_r)
		for i in range(self.leds_r):
			self.led_rects.append([
				self.win_width-self.leds_s,
				int(i*led_h)+self.leds_s,
				self.win_width,
				int(i*led_h)+self.leds_s+led_h
			])

		# bottom
		led_w = int((self.win_width-2*self.leds_s) / self.leds_b)
		for i in reversed(range(self.leds_b)):
			self.led_rects.append([
				int(i*led_w)+self.leds_s+led_w,
				self.win_height-self.leds_s,
				int(i*led_w)+self.leds_s,
				self.win_height
			])

		# left
		led_h = int((self.win_height-2*self.leds_s) / self.leds_l)
		for i in reversed(range(self.leds_l)):
			self.led_rects.append([
				0,
				int(i*led_h)+self.leds_s,
				self.leds_s,
				int(i*led_h)+self.leds_s+led_h
			])

	# ------------------------------------------------------
	def menu_switch_led_type(self,event=None):
		self.draw_type = 'circle' if self.draw_type == 'rect' else 'rect'
		self.resetUI()

	# ------------------------------------------------------
	def menu_switch_led_ids(self,event=None):
		self.show_numbers = not self.show_numbers
		self.resetUI()

	# ------------------------------------------------------
	def initUI(self):
		self.parent.title("LED-Simulator");
		tk.Frame.__init__(self, self.parent)
		self.pack(fill=tk.BOTH, expand=1)

		menubar = tk.Menu(self)

		filemenu = tk.Menu(menubar, tearoff=0)
		filemenu.add_command(label="Quit", accelerator="Ctrl+Q",  command=self.on_close)

		settingsmenu = tk.Menu(menubar, tearoff=0)
		settingsmenu.add_command(label="switch led type",  accelerator="t", command=self.menu_switch_led_type)
		settingsmenu.add_command(label="show/hide led IDs",  accelerator="n", command=self.menu_switch_led_ids)
		#settingsmenu.add_command(label="led size +1", accelerator="+", command=self.menu_led_size_inc)
		#settingsmenu.add_command(label="led size -1", accelerator="-", command=self.menu_led_size_dec)

		menubar.add_cascade(label="File", menu=filemenu)
		menubar.add_cascade(label="Settings", menu=settingsmenu)
		self.parent.config(menu=menubar)

		self.bind_all("<Control-q>", self.on_close)
		self.bind_all("t", self.menu_switch_led_type)
		self.bind_all("n", self.menu_switch_led_ids)
		#self.bind_all("+", self.menu_led_size_inc)
		#self.bind_all("-", self.menu_led_size_dec)

		self.frameC = tk.Frame(master=self)
		self.frameC.pack()
		self.statusbar = StatusBar(self)

		self.initCanvas()

	def initCanvas(self):
		self.canvas = tk.Canvas(self.frameC, width=self.win_width, height=self.win_height)
		self.canvas.pack()
		self.canvas.create_rectangle(0, 0, self.win_width, self.win_height, fill="darkgray", outline="darkgray")

		for idx, r in enumerate(self.led_rects):
			if r is None:
				self.led_widgets.append(None)
			else:
				if self.draw_type == 'circle':
					self.led_widgets.append(self.canvas.create_oval(r[0], r[1], r[2], r[3], fill="black", outline="darkgray"))
				else:
					self.led_widgets.append(self.canvas.create_rectangle(r[0], r[1], r[2], r[3], fill="black", outline="darkgray"))

				if self.show_numbers:
					angle = 0
					if (r[1] == 0 or r[3] == self.win_height):
						angle = 90
					self.canvas.create_text( int((r[0]+r[2])/2), int((r[1]+r[3])/2), font=("Purisa", 10), anchor=tk.CENTER, text=str(idx), angle=angle)

	# ------------------------------------------------------
	def on_close(self,event=None):
		self.udpServer.stop()
		self.udpServer.join()
		self.parent.destroy()

	# ------------------------------------------------------
	def updateLeds(self, led_data):
		#if self.show_video:
		#	ret, frame = self.vid.get_frame()

		#	if ret:
		#		image = Image.fromarray(frame)
		#		image = image.resize((self.win_width-2*self.leds_s, self.win_height-2*self.leds_s),0)
		#		self.photo = ImageTk.PhotoImage(image)
		#		self.canvas.create_image(self.leds_s, self.leds_s, image = self.photo, anchor = tk.NW)

		self.led_data = led_data
		color = lambda i,c:self.clut[c][ led_data[i][c] ]

		for idx in range(len(led_data)):
			if idx < len(self.led_widgets):
				if self.led_widgets[idx] is not None:
					self.canvas.itemconfigure(self.led_widgets[idx], fill="#%02x%02x%02x" % (color(idx,0), color(idx,1), color(idx,2) ) )

	# ------------------------------------------------------
	def menu_led_size_dec(self,event=None):
		self.leds_s = self.leds_s-1
		self.resetVars()
		self.resetUI()
		print(self.leds_s)

	# ------------------------------------------------------
	def menu_led_size_inc(self,event=None):
		self.leds_s = self.leds_s+1
		self.resetVars()
		self.resetUI()
		print(self.leds_s)

	# ------------------------------------------------------
	def calculateCLUTs(self):
		for c in range(3):
			for i in range(256):
				self.clut[c][i] = int(min(255,(i ** self.gamma) * self.whitepoint[c] ))

	# ------------------------------------------------------
	def setColorCorrection(self, gamma, whitepoint):
		self.gamma = gamma
		self.whitepoint = whitepoint

		self.calculateCLUTs()
		self.updateLeds(self.led_data)

	# ------------------------------------------------------
	def read_cfg(self,cfg='ambi-tv-ledsim.conf'):
		with codecs.open(cfg, 'r', encoding='utf-8', errors='ignore') as f:
			data = f.readlines()
			for line in data:
				if "leds-top" in line:
					line = ' '.join(line.split())
					leds = line.split(' ')[1].split('-')
					self.leds_t = abs(int(leds[0])-int(leds[1]))+1
					print(self.leds_t)
				if "leds-bottom" in line:
					line = ' '.join(line.split())
					leds = line.split(' ')[1].split('-')
					self.leds_b = abs(int(leds[0])-int(leds[1]))+1
					print(self.leds_b)
				if "leds-left" in line:
					line = ' '.join(line.split())
					leds = line.split(' ')[1].split('-')
					self.leds_l = abs(int(leds[0])-int(leds[1]))+1
					print(self.leds_l)
				if "leds-right" in line:
					line = ' '.join(line.split())
					leds = line.split(' ')[1].split('-')
					self.leds_r = abs(int(leds[0])-int(leds[1]))+1
					print(self.leds_r)
