import splash
from time import sleep

description = "Test various supported video codecs"

def run():
    splash.set_global("replaceObject", ["image", "image_ffmpeg", "object"])
    splash.set_object("image", "file", "./assets/tornado_h264.mov")
    sleep(10.0)
    splash.set_object("image", "file", "./assets/tornado_hap.mov")
    sleep(10.0)
    splash.set_object("image", "file", "./assets/tornado_hap_alpha.mov")
    sleep(10.0)
    splash.set_object("image", "file", "./assets/tornado_hap_q.mov")
    sleep(10.0)
    splash.set_global("replaceObject", ["image", "image", "object"])
