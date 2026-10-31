import glob,os,sys
from PIL import Image
os.chdir(sys.argv[1])
for f in glob.glob('shots/*.ppm'):
    Image.open(f).resize((640,480),Image.NEAREST).save('png/'+os.path.basename(f)[:-4]+'.png')
def sheet(names,cols,out):
    rows=(len(names)+cols-1)//cols
    W=Image.new('RGB',(660*cols,500*rows),'#888')
    for i,n in enumerate(names):
        W.paste(Image.open('png/'+n+'.png'),((i%cols)*660+10,(i//cols)*500+10))
    W.resize((330*cols,250*rows)).save(out)
sheet(['osc1','osc2','osc3','osc4','osc5','osc6','flt1','flt2','mod1'],3,'sheet2.png')
sheet(['mod2','mod3','mod4','seq2','seq3','fx1','fx2','fx3','fx4','pre1','pre2','env'],3,'sheet3.png')
