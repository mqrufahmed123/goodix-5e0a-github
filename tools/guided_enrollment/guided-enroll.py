#!/usr/bin/python3
"""Guide standard fprintd enrollment, then compare placement acceptance."""
from pathlib import Path
import datetime
import getpass
import json
import re
import signal
import subprocess
import threading
import uuid
import gi
gi.require_version('Gtk','4.0')
from gi.repository import Gtk, GLib
from coverage_policy import BASELINE, VALIDATION, ENROLL, placement, region_results, ENROLL_POSITIONS, keep_new_enrollment
from animation import PlacementGuide

HERE=Path(__file__).resolve().parent
class Guide(Gtk.Application):
    def __init__(self,reuse_session=None):
        super().__init__(application_id='local.goodix.FullPadEnrollment')
        self.connect('activate',self.activate)
        self.process=None; self.busy=False; self.phase='idle'; self.index=0
        self.training_started=False; self.backed_up=False; self.closing=False
        self.before=[]; self.after=[]; self.negative=[]; self.enroll_events=[]
        self.session=datetime.datetime.now().strftime('%Y%m%dT%H%M%S')+'-'+uuid.uuid4().hex[:8]
        self.finger='right-index-finger'
        self.reuse_session=reuse_session
        self.reuse_before=None
        if reuse_session:
            if not re.fullmatch(r'[0-9]{8}T[0-9]{6}-[a-f0-9]{8}',reuse_session):
                raise ValueError('Invalid baseline session.')
            old=json.loads((HERE/('session-'+reuse_session+'.json')).read_text())
            if (old.get('finger')!=self.finger or not old.get('decision','').startswith('restored-original:')
                    or len(old.get('before',[]))!=5 or old.get('baseline_positions')!=BASELINE):
                raise ValueError('A completed restored comparison with the same five baseline positions is required.')
            self.reuse_before=old['before']
    def activate(self,_):
        self.window=Gtk.ApplicationWindow(application=self,title='Goodix — guided fingertip enrollment')
        self.window.set_default_size(840,890)
        self.window.connect('close-request',self.close_requested)
        box=Gtk.Box(orientation=Gtk.Orientation.VERTICAL,spacing=15)
        for side in ['top','bottom','start','end']: getattr(box,'set_margin_'+side)(24)
        self.window.set_child(box)
        title=Gtk.Label(label='Teach the tip, sides and center',xalign=0)
        title.add_css_class('title-1'); box.append(title)
        explanation=Gtk.Label(label='Overlapping touches feed Goodix’s composite-template builder. The drawing is a placement guide, not an image of your fingerprint or a measured coverage percentage.',wrap=True,xalign=0)
        box.append(explanation)
        self.summary=Gtk.Label(label='RIGHT INDEX only: 5 baseline presses → 12 guided touches → 15 placement checks → 3 actual RIGHT MIDDLE checks.',wrap=True,xalign=0)
        box.append(self.summary)
        self.heading=Gtk.Label(label='Ready',wrap=True,xalign=0)
        self.heading.add_css_class('title-2'); box.append(self.heading)
        self.detail=Gtk.Label(label='Close the GNOME Fingerprint Login dialog first. Use only the selected finger throughout positive tests and enrollment. Fully lift between presses.',wrap=True,xalign=0)
        box.append(self.detail)
        self.guide=PlacementGuide(); box.append(self.guide)
        box.append(Gtk.Label(label='Blue = where to place the sensor on your pad · Green = guided touch accepted',wrap=True,xalign=0))
        self.actual_middle=Gtk.CheckButton(label='I have switched to my actual RIGHT MIDDLE finger for this rejection test')
        self.actual_middle.set_visible(False); box.append(self.actual_middle)
        self.progress=Gtk.ProgressBar(show_text=True); box.append(self.progress)
        self.status=Gtk.Label(label='Your current selected enrollment will be backed up before any replacement.',wrap=True,xalign=0)
        box.append(self.status)
        buttons=Gtk.Box(spacing=12)
        self.next=Gtk.Button(label='Back up and start comparison')
        self.next.add_css_class('suggested-action'); self.next.connect('clicked',self.advance)
        self.cancel=Gtk.Button(label='Cancel'); self.cancel.connect('clicked',self.cancel_clicked)
        buttons.append(self.next); buttons.append(self.cancel); box.append(buttons)
        self.window.present()
    def text(self,heading,detail,status=''):
        self.heading.set_text(heading); self.detail.set_text(detail); self.status.set_text(status)
    def region(self,key):
        self.guide.region(key)
    def save(self,decision=None):
        result=dict(session=self.session,finger=self.finger,phase=self.phase,before=self.before,after=self.after,
                    negative=self.negative,enrollment_events=self.enroll_events,coverage_percentage=None,
                    placement_results=region_results(self.after),negative_finger='actual-right-middle',
                    baseline_positions=BASELINE,validation_positions=VALIDATION)
        result['enrollment_positions']=ENROLL
        if self.reuse_session: result['baseline_reused_from']=self.reuse_session
        if decision: result['decision']=decision
        (HERE/('session-'+self.session+'.json')).write_text(json.dumps(result,indent=2)+'\n')
    def run_process(self,args,done,line=None,timeout=45):
        self.next.set_sensitive(False)
        self.busy=True
        def worker():
            try:
                if self.closing and 'restore' not in args:
                    self.busy=False; GLib.idle_add(done,-1,'Cancelled'); return
                p=subprocess.Popen(args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,bufsize=1)
                self.process=p; output=[]
                def interrupt():
                    if p.poll() is None: p.send_signal(signal.SIGINT)
                timer=threading.Timer(timeout,interrupt); timer.start()
                kill_timer=threading.Timer(timeout+5,lambda: p.kill() if p.poll() is None else None); kill_timer.start()
                for value in p.stdout:
                    output.append(value)
                    if line: GLib.idle_add(line,value.strip())
                code=p.wait(); timer.cancel(); kill_timer.cancel()
                self.process=None; self.busy=False
                GLib.idle_add(done,code,''.join(output))
            except Exception as error:
                self.process=None; self.busy=False; GLib.idle_add(done,-1,str(error))
        threading.Thread(target=worker,daemon=True).start()
    def protected(self,action,done):
        args=['pkexec','--disable-internal-agent','/usr/bin/python3',str(HERE/'protected-template.py'),action,self.session,self.finger]
        if action=='backup' and self.reuse_session: args.append(self.reuse_session)
        self.run_process(args,done,timeout=120)
    def advance(self,_):
        if self.phase=='idle':
            self.text('Backing up '+self.finger.replace('-',' '),'Authenticate the system backup prompt. Only the selected template is copied into a root-only recovery directory.')
            self.protected('backup',self.backup_done)
        elif self.phase in ('before','after','negative'): self.verify()
        elif self.phase=='enroll-ready': self.enroll()
        elif self.phase=='done': self.window.close()
    def backup_done(self,code,output):
        if self.closing:
            self.window.destroy(); return
        if code:
            self.phase='idle'; self.next.set_sensitive(True)
            self.text('Backup did not complete',output[-1200:]); return
        self.backed_up=True
        if self.reuse_before is not None:
            self.before=list(self.reuse_before); self.phase='enroll-ready'; self.save()
            self.text('Original baseline verified unchanged',
                      'The protected backup matches the previous comparison. Next: the revised twelve overlapping touches.',
                      'Use the RIGHT INDEX. Keep each neighboring patch overlapping; lift between enrollment touches.')
            self.next.set_label('Start 12-touch guided enrollment'); self.next.set_sensitive(True)
        else:
            self.phase='before'; self.index=0; self.show_position()
    def show_position(self):
        self.save()
        if self.phase=='negative':
            self.region(''); name='RIGHT MIDDLE finger — different finger'
            instruction='Use your actual right middle finger, not the enrolled index/thumb. These scans must be rejected.'
            count=3
            self.actual_middle.set_visible(True); self.actual_middle.set_active(False)
        else:
            sequence=BASELINE if self.phase=='before' else VALIDATION
            key,name,instruction=placement(sequence[self.index]); self.region(key); count=len(sequence)
            self.actual_middle.set_visible(False)
        self.text(f'{self.phase.title()} check {self.index+1}/{count}: '+name,
                  instruction,'Press “Scan this position”, then touch once and fully lift. Selected enrollment: '+self.finger)
        self.next.set_label('Scan this position'); self.next.set_sensitive(True)
        self.progress.set_fraction(self.index/count); self.progress.set_text(f'{self.index}/{count} comparison results')
    def verify(self):
        if self.phase=='negative' and not self.actual_middle.get_active():
            self.status.set_text('Switch to your actual RIGHT MIDDLE finger and tick the confirmation before scanning.'); return
        self.status.set_text('Ready for one press. Fully lift afterward.')
        self.run_process(['stdbuf','-oL','fprintd-verify','-f',self.finger,getpass.getuser()],self.verify_done)
    def verify_done(self,code,output):
        if self.closing: self.restore('cancelled'); return
        match=re.search(r'Verify result: (verify-match|verify-no-match) \(done\)',output)
        if not match:
            self.text('Scan did not complete',output[-1100:],'Close competing fingerprint dialogs, then retry this position.')
            self.next.set_sensitive(True); return
        rows=getattr(self,self.phase); rows.append(match[1]); self.index+=1; self.save()
        count=3 if self.phase=='negative' else (len(BASELINE) if self.phase=='before' else len(VALIDATION))
        if self.index<count:
            self.show_position(); self.status.set_text('Previous result: '+match[1]+'. Fully lift, then prepare the next position.'); return
        if self.phase=='before':
            self.phase='enroll-ready'
            self.text(f'Current enrollment: {self.before.count("verify-match")}/5 positions accepted',
                      'Next, enroll the SAME '+self.finger.replace('-',' ')+'. Follow each highlighted region, keep neighboring patches overlapping, and lift fully between touches.',
                      'The current template is safely backed up. The other enrolled finger will not be changed.')
            self.next.set_label('Start 12-touch guided enrollment'); self.next.set_sensitive(True)
        elif self.phase=='after':
            self.phase='negative'; self.index=0; self.show_position()
        else: self.evaluate()
    def enroll(self):
        self.phase='enrolling'; self.training_started=True; self.enroll_passed=0
        self.enroll_instruction()
        self.run_process(['stdbuf','-oL','fprintd-enroll','-f',self.finger,getpass.getuser()],self.enroll_done,self.enroll_line,timeout=300)
    def enroll_instruction(self):
        if self.enroll_passed >= 12:
            self.text('Twelve touches accepted','Saving the composite template. Lift your finger and wait.')
            return
        key,name,instruction=ENROLL_POSITIONS[min(self.enroll_passed,11)]
        if self.enroll_passed>0 and ENROLL[self.enroll_passed-1]==key:
            instruction+=' Keep this region, but rotate a few degrees for a second view after fully lifting.'
        self.region(key)
        self.text(f'Touch {min(self.enroll_passed+1,12)}/12: '+name,instruction,
                  'Use '+self.finger.replace('-',' ')+'. Hold lightly for about half a second, then fully lift. Follow the next region only after the accepted count advances.')
        self.progress.set_fraction(self.enroll_passed/12)
        self.progress.set_text(f'{self.enroll_passed}/12 accepted touches — not a coverage percentage')
    def enroll_line(self,line):
        if not line.startswith('Enroll result: '): return
        result=line.split(': ',1)[1]; self.enroll_events.append(result); self.save()
        if result=='enroll-stage-passed':
            self.guide.add(ENROLL[min(self.enroll_passed,11)])
            self.enroll_passed+=1; self.enroll_instruction()
        elif result not in ('enroll-completed',):
            self.status.set_text('Scanner requests retry: '+result+'. Fully lift and repeat the SAME region.')
    def enroll_done(self,code,output):
        if self.closing or code or 'Enroll result: enroll-completed' not in output:
            self.restore('cancelled' if self.closing else 'enrollment-failed'); return
        def staged(code,output):
            if self.closing or code:
                self.restore('candidate-backup-failed-or-cancelled'); return
            self.phase='after'; self.index=0; self.show_position()
            self.status.set_text('Now test each region three times. The candidate has its own protected backup; a failed comparison restores the original.')
        self.protected('stage',staged)
    def evaluate(self):
        if keep_new_enrollment(self.before,self.after,self.negative):
            def kept(code,output):
                if code: self.restore('keep-check-failed'); return
                self.phase='done'; self.training_started=False; self.save('kept-new-enrollment')
                self.actual_middle.set_visible(False)
                self.text('Guided enrollment kept',
                          'Center, tip, left, right and base: each matched 3/3. Actual right middle rejected: 3/3.',
                          'These are measured test results, not proof of complete anatomical coverage or a false-accept rate. The original backup remains available.')
                self.next.set_label('Finish'); self.next.set_sensitive(True)
            self.protected('keep',kept)
        else: self.restore('comparison-regressed-or-negative-failed')
    def restore(self,reason):
        if self.phase=='restoring': return
        if not self.training_started:
            self.phase='done'; self.save(reason)
            if self.closing: self.window.destroy()
            else:
                self.text('Comparison cancelled','Enrollment was not changed.')
                self.next.set_label('Finish'); self.next.set_sensitive(True)
            return
        self.phase='restoring'; self.text('Restoring the previous enrollment','Authenticate the restore prompt. Other enrolled fingers are preserved.')
        def finished(code,output):
            self.phase='done'; self.save(('restored-original: '+reason) if code==0 else 'restore-required')
            if code==0:
                self.training_started=False
                self.text('Previous enrollment restored',f'Baseline: {self.before.count("verify-match")}/5. Validation: {self.after.count("verify-match")}/15.\nReason: '+reason,
                          'The candidate remains in the protected backup for investigation. It is not active.')
                if self.closing: self.window.destroy(); return
            else:
                self.text('Restore needs attention',output[-1200:],f'Session: {self.session}. The protected original backup is retained; see README.md.')
                self.closing=False
            self.next.set_label('Finish'); self.next.set_sensitive(True)
        self.protected('restore',finished)
    def cancel_clicked(self,_):
        self.closing=True
        if self.process and self.process.poll() is None: self.process.send_signal(signal.SIGINT)
        else: self.restore('cancelled')
    def close_requested(self,_):
        if self.busy or self.training_started:
            self.cancel_clicked(None); return True
        return False

if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser()
    parser.add_argument('--preview',action='store_true')
    parser.add_argument('--reuse-baseline-session')
    args=parser.parse_args()
    app=Guide(args.reuse_baseline_session)
    if args.preview:
        GLib.timeout_add_seconds(3, app.quit)
    app.run([])
