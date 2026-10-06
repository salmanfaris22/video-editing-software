import json,sys,collections
items=json.load(open(sys.argv[1]))
# ---- phase catalogue: id -> (title, goal)
P=collections.OrderedDict([
 ('0.1',('Ship the product','Signing, installers, updates, licences, Windows on real hardware, performance basics')),
 ('0.2',('Privacy and redaction','Safe screen recordings: blur secrets, consent, local-first')),
 ('0.3',('Recorder must-haves','Cursor and click data, region/window capture, pause, retakes, warnings')),
 ('1.1',('GPU render path','Same picture on the GPU, real-time preview')),
 ('1.2',('Float pipeline and color management','Linear float, OCIO, camera color spaces, HDR inputs')),
 ('1.3',('Corrector layers (format v3)','Several grades per clip, timeline grade, keyframed grades')),
 ('1.4',('Primaries (Resolve wheels)','Lumetri/Resolve primaries: wheels, contrast/pivot, shadows/highlights, auto balance')),
 ('1.5',('Scopes','Waveform, parade, vectorscope, histogram')),
 ('1.6',('Curves','Custom and hue/sat/lum curves')),
 ('1.7',('Qualifier and secondaries','Select by color, matte view, vector secondaries')),
 ('1.8',('Power windows','Shape masks with feather')),
 ('1.9',('Tracking','Track windows and effects')),
 ('1.10',('AI subject mask','Person/object masks, face refinement')),
 ('1.11',('Looks, LUTs and gallery','Looks gallery, stills, LUT handling, film looks')),
 ('1.12',('Shot match and auto color','Match clips, auto balance')),
 ('1.13',('Nodes','Node graph on top of corrector layers')),
 ('1.14',('Color workspace','Resolve-style Color page layout and palettes')),
 ('1.15',('Image repair','Noise reduction, sharpen, deflicker, stabilize, lens fixes')),
 ('2.1',('MCP core and all tools','Every feature reachable by Claude, ChatGPT, Codex (see AI_ASSISTANTS_MCP)')),
 ('2.2',('Agent perception and analysis','Scene/shot/content analysis the agent uses to judge edits')),
 ('2.3',('Speed ramps and time','Speed, ramps, freeze, frame interpolation')),
 ('2.4',('Smooth transitions','Transition library and smooth jump cuts')),
 ('2.5',('Focus: zoom, reframe, follow','Auto zoom, cursor follow, reframe, punch-in, eye contact')),
 ('2.6',('Editor recipes and AI editing','Transcript editing, pauses/fillers, chapters, shorts, ducking')),
 ('3.1',('Effect system and Effect Controls','Effect registry, stacks, adjustment layers, presets, effects browser')),
 ('3.2',('Blur, sharpen, redaction effects','Blur family, mosaic, tracked redaction')),
 ('3.3',('Transform and distort','Rotation, crop, flip, corner pin, lens, drop shadow on any layer')),
 ('3.4',('Stylize, light and generate','Glow, grain, light leaks, gradients, shapes')),
 ('3.5',('Keying','Chroma/luma key, spill, AI background removal')),
 ('3.6',('Compositing','Blend modes, masks, nesting, alpha')),
 ('3.7',('Animation and graph editor','Keyframes on everything, easing, graph editor, motion paths')),
 ('3.8',('Text, titles and motion graphics','Text styling, Character panel, shapes, templates, callouts, animated captions look')),
 ('4.1',('Editing tools','Ripple/roll/slip/slide, insert/overwrite, multicam, nesting, copy/paste')),
 ('4.2',('Audio essentials','Voice cleanup, loudness, EQ, compressor, envelopes, mixer, voice-over')),
 ('4.3',('Captions and subtitles','Transcription captions, styles, translation, formats')),
 ('5.1',('Workspace and panels','Pro mode layout, docking, workspaces, command palette')),
 ('5.2',('Tools bar','Selection, hand, zoom, shapes, pen, type, edit tools')),
 ('5.3',('Viewer','Zoom, guides, safe areas, compare, on-viewer controls')),
 ('5.4',('Media and project panel','Bins, metadata, relink, proxies, import formats')),
 ('5.5',('Pro timeline','Switches, modes, properties and keyframes in the timeline, markers')),
 ('5.6',('Other panels and app-specific UI','Align, tracker, history, inspector, page tabs')),
 ('5.7',('Visual language and shortcuts','Icons, tooltips, scrubby fields, density, accessibility')),
 ('6.1',('Delivery and sharing','Formats, presets, chapters, share pages, uploads')),
 ('6.2',('Collaboration and review','Comments, versions, team features')),
 ('7.1',('Project, settings, performance, platform','Preferences, templates, caching, playback resolution, Windows/Linux')),
 ('8.1',('Specialist: effects, VFX, 3D and motion','3D layers, particles, simulation, expressions, puppet, paint, rare effects')),
 ('8.2',('Specialist: color finishing and HDR','HDR mastering, RAW, warper, stereo, control surfaces, advanced nodes')),
 ('8.3',('Specialist: audio post','Surround, buses, plug-in hosting, ADR')),
 ('8.4',('Specialist: platform and ecosystem','Linux, iPad, scripting API, SDK, enterprise, collaboration')),
 ('8.5',('Specialist: AI and automation','Generative and analysis features beyond the core agent')),
 ('8.6',('Specialist: editing and captions','Cut-page tools, broadcast captions, rare edit modes')),
 ('8.7',('Specialist: pro UI extras','Camera/3D tools, AE-only switches, rare panels')),
])
OV={ # id-level overrides: (doc,id) -> phase
 ('COLOR','1.1'):'1.1',('COLOR','1.15'):'1.1',('COLOR','1.13'):'7.1',('COLOR','1.14'):'5.4',
 ('COLOR','1.10'):'3.1',('COLOR','1.11'):'3.1',('COLOR','1.12'):'3.1',
 ('COLOR','2.24'):'1.12',('COLOR','2.9'):'1.12',('COLOR','2.27'):'1.14',('COLOR','2.23'):'1.11',('COLOR','2.25'):'1.3',('COLOR','2.26'):'1.4',
 ('COLOR','4.6'):'1.8',('COLOR','4.7'):'1.8',('COLOR','4.8'):'1.9',('COLOR','4.9'):'1.10',('COLOR','4.10'):'1.10',('COLOR','4.12'):'1.10',
 ('COLOR','5.2'):'1.13',('COLOR','5.3'):'1.13',('COLOR','5.4'):'1.13',('COLOR','5.7'):'1.13',('COLOR','5.8'):'1.11',
 ('COLOR','8.14'):'1.15',('COLOR','8.12'):'1.10',
 ('COLOR','9.10'):'3.2',('COLOR','9.11'):'3.2',
 ('COLOR','10.11'):'2.5',('COLOR','10.12'):'2.5',('COLOR','10.13'):'2.5',('COLOR','10.14'):'3.3',
 ('COLOR','11.9'):'3.8',('COLOR','11.12'):'3.8',('COLOR','12.7'):'3.5',
 ('COLOR','16.8'):'1.9',('COLOR','16.3'):'3.7',
 ('COLOR','18.1'):'1.12',('COLOR','18.2'):'1.12',('COLOR','18.3'):'1.10',('COLOR','18.4'):'1.10',('COLOR','18.9'):'2.5',('COLOR','18.10'):'2.3',('COLOR','18.7'):'1.15',('COLOR','18.8'):'1.15',
 ('DR','4.2'):'1.12',('DR','4.3'):'1.4',('DR','4.7'):'1.6',('DR','4.10'):'1.7',('DR','4.11'):'1.8',('DR','4.12'):'1.9',('DR','4.13'):'1.10',('DR','4.6'):'1.15',('DR','4.14'):'3.2',('DR','4.19'):'1.5',('DR','4.18'):'1.3',('DR','4.16'):'3.3',
 ('DR','8.9'):'1.3',('DR','8.11'):'1.3',('DR','9.1'):'1.11',('DR','9.2'):'1.11',('DR','9.3'):'1.11',
 ('AUDIT','2.17'):'2.6',('AUDIT','2.18'):'2.6',('AUDIT','2.19'):'2.6',('AUDIT','2.20'):'2.5',('AUDIT','2.22'):'2.3',('AUDIT','2.16'):'2.2',
 ('AUDIT','3.11'):'2.6',('AUDIT','3.20'):'4.3',('AUDIT','4.14'):'2.5',('AUDIT','4.15'):'2.5',('AUDIT','4.13'):'3.8',('AUDIT','4.9'):'1.9',
 ('AUDIT','7.1'):'2.6',('AUDIT','7.2'):'4.2',('AUDIT','7.3'):'2.6',('AUDIT','7.4'):'2.5',('AUDIT','7.5'):'1.10',('AUDIT','7.6'):'2.5',('AUDIT','7.8'):'2.2',('AUDIT','7.10'):'2.6',('AUDIT','7.12'):'2.6',('AUDIT','7.9'):'2.6',
 ('AUDIT','1.7'):'5.4',('AUDIT','1.8'):'7.1',('AUDIT','1.16'):'4.3',
 ('AUDIT','5.13'):'6.1',('AUDIT','17.2'):'3.2',
 ('UI','9.1'):'3.1',('UI','5.1'):'3.1',('UI','5.2'):'3.1',('UI','5.3'):'3.1',('UI','5.4'):'3.7',('UI','5.5'):'5.7',('UI','5.6'):'3.1',('UI','5.7'):'3.1',('UI','5.8'):'8.1',('UI','5.9'):'3.1',
 ('UI','9.6'):'1.9',('UI','9.12'):'1.5',('UI','9.15'):'6.1',('UI','9.3'):'4.2',('UI','13.2'):'1.14',('UI','13.3'):'1.4',('UI','13.4'):'3.8',('UI','13.5'):'2.6',('UI','11.0'):'5.7',
}
SEC={ # (doc,sec) -> default phase
 ('COLOR',1):'1.2',('COLOR',2):'1.4',('COLOR',3):'1.6',('COLOR',4):'1.7',('COLOR',5):'1.3',('COLOR',6):'1.5',('COLOR',7):'1.11',('COLOR',8):'1.15',
 ('COLOR',9):'3.2',('COLOR',10):'3.3',('COLOR',11):'3.4',('COLOR',12):'3.5',('COLOR',13):'3.6',('COLOR',14):'2.4',('COLOR',15):'2.3',('COLOR',16):'3.7',('COLOR',17):'3.8',('COLOR',18):'2.2',('COLOR',19):'3.1',
 ('UI',1):'5.1',('UI',2):'5.2',('UI',3):'5.3',('UI',4):'5.4',('UI',5):'3.1',('UI',6):'5.5',('UI',7):'3.7',('UI',8):'3.8',('UI',9):'5.6',('UI',12):'5.7',('UI',13):'5.6',
 ('DR',1):'1.14',('DR',2):'1.14',('DR',3):'1.14',('DR',4):'1.14',('DR',5):'1.4',('DR',6):'1.5',('DR',8):'1.13',('DR',9):'1.11',
 ('AUDIT',1):'5.4',('AUDIT',2):'4.1',('AUDIT',3):'4.2',('AUDIT',4):'3.8',('AUDIT',5):'6.1',('AUDIT',6):'4.3',('AUDIT',7):'2.6',('AUDIT',8):'6.2',('AUDIT',9):'7.1',('AUDIT',10):'7.1',('AUDIT',11):'7.1',('AUDIT',16):'0.3',('AUDIT',17):'0.2',('AUDIT',18):'0.1',
}
SPECIAL={'1':'8.2','2':'8.1','3':'8.1','4':'8.1','5':'8.1','6':'8.3','7':'8.4','0':'8.4'}
def special(i,base):
    st=base.split('.')[0]
    if st=='1': return '8.2'
    if st=='3': return '8.1'
    if st=='2': return '8.5'
    if base=='4.2': return '8.3'
    if base in('4.1','4.3'): return '8.6'
    if st=='5' and base!='5.4': return '8.7'
    return '8.4'
out=collections.defaultdict(list); done=[]
for i in items:
    if i['status']=='✅': done.append(i); continue
    base=OV.get((i['doc'],i['id'])) or SEC[(i['doc'],i['sec'])]
    if i['prio']=='P3':
        ph=special(i,base)
    else:
        ph=base+('b' if i['prio']=='P2' else 'a')
    i['phase']=ph; out[ph].append(i)
json.dump({'phases':P,'out':out,'done':done},open(sys.argv[2],'w'),ensure_ascii=False)
tot=sum(len(v) for v in out.values())
print('open items',tot,'done',len(done),'phases used',len(out))
for k in sorted(out,key=lambda x:[int(p) if p.isdigit() else p for p in x.replace('a','.0').replace('b','.1').split('.')]): print(k,len(out[k]),end='  ')
