from openalea.plantgl.all import *
from PyQt4.QtCore import *
from PyQt4.QtGui import *
import sys

app = QApplication([])

def test_projpershape():
    try:
        sc = Scene()
    except:
        print 'Could not create Scene()'
    
    try:
        s = Shape(Sphere())
    except:
        print 'Could not create Shape(Sphere())'

    s.id = 100
    sc += s

    try:
        s = Shape(Translated(1,2,1,Sphere()))
    except:
        print 'issue with Shape(Translated)'

    s.id = 200

    sc += s

    Viewer.display(sc)    
    pixpershape, pixsize = Viewer.frameGL.getProjectionPerShape()
    print pixpershape
    print pixsize
    print '---------'
    pixpershape = dict(pixpershape)
    assert len(pixpershape) == len(sc)
    keys1 = [sh.id for sh in sc]
    keys1.sort()
    keys2 = pixpershape.keys()
    keys2.sort()
    assert keys1 == keys2


def test_projpershape_nullid():
    s = Shape(Sphere())
    s.id = 0
    Viewer.display(s)    
    pixpershape, pixsize = Viewer.frameGL.getProjectionPerShape()    
    print pixpershape
    print pixsize
    assert len(pixpershape) == 1
    assert pixpershape[0][0] == s.id
    
def test_projpershape_bigid():
    s = Shape(Sphere())
    s.id = 0x0f0f0f0f
    Viewer.display(s)    
    pixpershape, pixsize = Viewer.frameGL.getProjectionPerShape()    
    print s.id,pixpershape
    print pixsize
    assert len(pixpershape) == 1
    assert pixpershape[0][0] == s.id

def test_projpershape_2bigid():
    sc = Scene()
    s = Shape(Translated(-2,0,0.5,Sphere()))
    s.id = 0x0f0f0f0f
    c = Color4.fromUint(s.id)
    s.appearance = Material(Color3(c.red,c.green,c.blue),1)
    s.appearance.transparency = c.alpha/255.
    sc += s
    s = Shape(Sphere())
    s.id = 0x0f0f0000
    c = Color4.fromUint(s.id)
    s.appearance = Material(Color3(c.red,c.green,c.blue),1)
    s.appearance.transparency = c.alpha/255.
    sc += s
    Viewer.display(sc)
    pixpershape, pixsize = Viewer.frameGL.getProjectionPerShape()    
    print pixpershape, pixsize
    pixpershape = dict(pixpershape)
    assert len(pixpershape) == len(sc)
    keys1 = [sh.id for sh in sc]
    keys1.sort()
    keys2 = pixpershape.keys()
    keys2.sort()
    print keys1 , keys2
    assert keys1 == keys2
    
if __name__ == '__main__':
    test_projpershape()
    test_projpershape_nullid()
    test_projpershape_bigid()
    test_projpershape_2bigid()
    
    sys.exit(app.exec_())
