"""Animated placement schematic. Ridges are illustrative, never biometric data."""
import math
import time
import cairo
import gi
gi.require_version('Gtk', '4.0')
from gi.repository import Gtk
from coverage_policy import REGIONS

def draw_guide(ctx, width, height, target, accepted, elapsed):
    ctx.set_source_rgb(.08, .10, .14); ctx.paint()
    scale = min(width / 660, height / 330)
    ctx.translate(width/2, height/2); ctx.scale(scale, scale)
    # A fingertip outline and stylized ridges, clipped inside the outline.
    def outline():
        ctx.new_path(); ctx.move_to(-93, 120)
        ctx.curve_to(-110, 10, -100, -140, 0, -140)
        ctx.curve_to(100, -140, 110, 10, 93, 120)
        ctx.curve_to(35, 153, -35, 153, -93, 120)
        ctx.close_path()
    ctx.save(); outline(); ctx.clip()
    ctx.set_line_width(1.5)
    for i in range(5, 33):
        ctx.save(); ctx.scale(.76, 1)
        ctx.set_source_rgba(.45, .55, .65, .48)
        ctx.arc(0, 20, i*7, 0, 2*math.pi); ctx.stroke(); ctx.restore()
    # Green marks mean an accepted guided touch, not measured skin coverage.
    for key in accepted:
        x,y = REGIONS[key][2]
        ctx.set_source_rgba(.20, .80, .55, .20)
        ctx.arc(x*100, y*150, 28, 0, 2*math.pi); ctx.fill()
    if target in REGIONS:
        x,y = REGIONS[target][2]
        pulse = .58 + .22*math.sin(elapsed*4)
        ctx.set_source_rgba(.25, .65, 1, pulse)
        ctx.arc(x*100, y*150, 31 + 2*math.sin(elapsed*4), 0, 2*math.pi); ctx.fill()
        ctx.set_source_rgb(.55, .83, 1); ctx.set_line_width(2.5)
        ctx.rectangle(x*100-31, y*150-22, 62, 44); ctx.stroke()
    ctx.restore(); outline()
    ctx.set_source_rgb(.65, .75, .85); ctx.set_line_width(2); ctx.stroke()
    ctx.select_font_face('Sans', cairo.FONT_SLANT_NORMAL, cairo.FONT_WEIGHT_NORMAL)
    ctx.set_font_size(15); ctx.set_source_rgb(.80, .86, .93)
    for text, x, y in [('TIP', -14, -151), ('BASE', -20, 157), ('LEFT', -164, 5), ('RIGHT', 117, 5)]:
        ctx.move_to(x,y); ctx.show_text(text)
    # Animated press / lift cue on a separate side-on schematic.
    offset = 13*(1-math.cos(elapsed*2))/2
    ctx.set_source_rgb(.22,.30,.39); ctx.rectangle(210,75,80,12); ctx.fill()
    ctx.set_source_rgb(.42,.73,1); ctx.rectangle(225,35-offset,50,26); ctx.fill()
    ctx.set_source_rgb(.8,.86,.93); ctx.move_to(205,115); ctx.show_text('Touch, then lift')

class PlacementGuide(Gtk.DrawingArea):
    def __init__(self):
        super().__init__()
        self.target='center'; self.accepted=set(); self.began=time.monotonic()
        self.set_content_width(660); self.set_content_height(330)
        self.set_draw_func(lambda _,ctx,w,h: draw_guide(ctx,w,h,self.target,self.accepted,time.monotonic()-self.began))
        self.add_tick_callback(self.tick)
    def tick(self,*_):
        self.queue_draw(); return True
    def region(self,key):
        self.target=key; self.began=time.monotonic(); self.queue_draw()
    def add(self,key):
        self.accepted.add(key); self.queue_draw()

if __name__ == '__main__':
    surface=cairo.ImageSurface(cairo.FORMAT_ARGB32,900,450)
    draw_guide(cairo.Context(surface),900,450,'tip',{'center'},.4)
    surface.write_to_png('animation-preview.png')
