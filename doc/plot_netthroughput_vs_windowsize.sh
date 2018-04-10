source `dirname "${BASH_SOURCE[0]}"`/plot_parser.sh


python <<EOF
import numpy as np
from mayavi import mlab

#from tvtk.api import tvtk
#from mayavi.scripts import mayavi2
#from mayavi.sources.vtk_data_source import VTKDataSource
#from mayavi.modules.outline import Outline
#from mayavi.modules.surface import Surface

#@mayavi2.standalone
@mlab.show
def main():
    data = np.loadtxt("$TMPDATA")

    x    = data[:,0] # ws
    y    = data[:,2] # ps
    z    = data[:,3] # T
    zerr = data[:,4] # error of T
    col  = data[:,1] # ack delay

    #pd = tvtk.PolyData()
    
    mlab.points3d(x, y, z, col, scale_mode='none')
    #mayavi.new_scene()
    #d = VTKDataSource()
    #d.data = (x,y,z)
    #mayavi.add_source(d)
    #mayavi.add_module(Outline())
    #s = Surface()
    #mayavi.add_module(s)
    #s.actor.property.set(representation='p', point_size=2)

if __name__ == '__main__':
    main()
EOF

#rm $TMPDATA
