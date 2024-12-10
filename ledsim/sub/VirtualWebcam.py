import cv2
import os
import subprocess
import threading
import pyvirtualcam
import numpy as np


class VirtualWebcam:
    def __init__(self, device='/dev/video10', width=640, height=480, fps=30, card_label="VirtualCam"):
        """
        Initialisiert die virtuelle Webcam und das virtuelle Gerät.
        """
        self.device = device
        self.width = width
        self.height = height
        self.fps = fps
        self.card_label = card_label
        self.cap = None
        self.running = False
        self.current_frame = None  # Speichert den aktuellen Frame
        self.loop_video = False  # Gibt an, ob ein Video wiederholt wird

        # Lade das Kernelmodul v4l2loopback, falls es nicht vorhanden ist
        if not os.path.exists(device):
            self._load_v4l2loopback()

    def _load_v4l2loopback(self):
        """
        Lädt das v4l2loopback Kernelmodul mit den gewünschten Parametern.
        """
        print(f"Lade v4l2loopback für Gerät {self.device[10:]}...")
        try:
            subprocess.run([
                "sudo", "modprobe", "v4l2loopback",
                f"devices=1",
                f"video_nr={self.device[10:]}",
                f"card_label={self.card_label}"
            ], check=True)
            print(f"Virtuelles Gerät {self.device} erfolgreich erstellt.")
        except subprocess.CalledProcessError as e:
            print("Fehler beim Laden von v4l2loopback:", e)
            raise RuntimeError("Konnte v4l2loopback nicht laden")

    def start_stream(self, frame_source=0, loop=False):
        """
        Startet den Stream an die virtuelle Webcam. Das frame_source kann eine Webcam-ID (z.B. 0),
        eine Videodatei oder eine andere Bildquelle sein. Wenn loop=True, wird das Video wiederholt.
        """
        if self.running:
            print("Stream läuft bereits!")
            return

        print(f"Starte virtuellen Webcam-Stream auf {self.device}...")
        self.cap = cv2.VideoCapture(frame_source)
        if not self.cap.isOpened():
            raise RuntimeError(f"Konnte die Videoquelle {frame_source} nicht öffnen.")

        #self.width = int(self.cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        #self.height = int(self.cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        #self.fps = self.cap.get(cv2.CAP_PROP_FPS)

        self.loop_video = loop
        self.running = True
        threading.Thread(target=self._stream_thread, daemon=True).start()

    def _stream_thread(self):
        """
        Führt den Stream im Hintergrund aus.
        """
        with pyvirtualcam.Camera(width=self.width, height=self.height, fps=self.fps, device=self.device, fmt=pyvirtualcam.PixelFormat.YUYV) as cam:
            print(f'Virtuelle Kamera gestartet: {cam.device}')
            
            while self.running:
                ret, frame = self.cap.read()
                if not ret:
                    if self.loop_video:
                        print("Ende des Videos erreicht, starte von vorne...")
                        self.cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
                        continue
                    else:
                        print("Keine Frames mehr verfügbar, Stream wird beendet.")
                        break

                # Frame bearbeiten und speichern
                frame = cv2.resize(frame, (self.width, self.height))
                frame = cv2.putText(frame, self.card_label, (50, 50),
                                    cv2.FONT_HERSHEY_SIMPLEX, 1, (255, 255, 255), 2)
                
                # Konvertiere von BGR (OpenCV) nach YUYV
                frame_yuyv = self._convert_bgr_to_yuyv(frame)

                # Sende den Frame an das virtuelle Gerät
                cam.send(frame_yuyv)

                # Konvertiere von BGR (OpenCV) nach RGB (Canvas)
                frame_rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)

                # Speichere den aktuellen Frame für Abruf
                self.current_frame = frame_rgb.copy()

                cam.sleep_until_next_frame()

    def _convert_bgr_to_yuyv(self, frame):
        """
        Konvertiert einen BGR-Frame (OpenCV-Standard) in das YUYV-Format.
        """
        # Konvertiere von BGR nach YUV
        frame_yuv = cv2.cvtColor(frame, cv2.COLOR_BGR2YUV)
        y = frame_yuv[:, :, 0]
        u = frame_yuv[:, :, 1]
        v = frame_yuv[:, :, 2]

        # Erstelle YUYV-Array
        # Jeder zweite Pixelwert hat eine gemeinsame U- und V-Komponente
        yuyv = np.zeros((self.height, self.width * 2), dtype=np.uint8)
        yuyv[:, 0::4] = y[:, ::2]  # Y1
        yuyv[:, 1::4] = u[:, ::2]  # U
        yuyv[:, 2::4] = y[:, 1::2]  # Y2
        yuyv[:, 3::4] = v[:, ::2]  # V

        return yuyv

    def ___convert_bgr_to_yuyv(self, frame):
        """
        Konvertiert einen BGR-Frame (OpenCV-Standard) in das YUYV-Format.
        """
        frame_yuv = cv2.cvtColor(frame, cv2.COLOR_BGR2YUV)
        y = frame_yuv[:, :, 0]
        u = frame_yuv[:, :, 1]
        v = frame_yuv[:, :, 2]

        # Kombiniere Y, U und V zu einem YUYV-Frame
        yuyv = np.zeros((self.height, self.width, 2), dtype=np.uint8)
        yuyv[:, :, 0] = y  # Y-Werte
        yuyv[:, :, 1] = np.vstack((u.flatten()[::2], v.flatten()[::2])).T.flatten()  # Abwechselnd U und V

        return yuyv

    def get_frame(self):
        """
        Gibt den aktuellen Frame zurück (oder None, wenn noch kein Frame verfügbar ist).
        """
        return self.current_frame is not None, self.current_frame

    def stop(self):
        """
        Beendet den Stream und entfernt das Kernelmodul, wenn keine andere Anwendung es nutzt.
        """
        if not self.running:
            print("Stream läuft nicht.")
            return

        print("Beende virtuellen Webcam-Stream...")
        self.running = False
        if self.cap:
            self.cap.release()
            self.cap = None
        print("Stream beendet.")

        print(f"Entferne v4l2loopback für {self.device}...")
        try:
            subprocess.run(["sudo", "modprobe", "-r", "v4l2loopback"], check=True)
            print("v4l2loopback wurde erfolgreich entfernt.")
        except subprocess.CalledProcessError as e:
            print("Fehler beim Entfernen von v4l2loopback:", e)
            raise RuntimeError("Konnte v4l2loopback nicht entfernen")
