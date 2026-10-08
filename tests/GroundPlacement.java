import dev.skycraft.world.SkyRay;
import java.util.Arrays;
public class GroundPlacement {
 public static void main(String[] args) {
  for(double y=-512;y<20;y+=.0137){
   var hit=new SkyRay.Hit(.5,.5,y,.5,0,1,0,null);
   int bottom=SkyRay.placementCell(hit)[1];
   if(bottom>y+1e-6||bottom+1<y)throw new AssertionError("Floating/buried cell at "+y+": "+bottom);
  }
  var ground=new SkyRay.Hit(.5,.5,10.8,.5,0,1,0,null);
  if(!Arrays.equals(SkyRay.placementCell(ground),new int[]{0,10,0}))throw new AssertionError("Regression at fractional ground");
  var wall=new SkyRay.Hit(.5,10.9,2.2,4.5,1,0,0,null);
  if(!Arrays.equals(SkyRay.placementCell(wall),new int[]{11,2,4}))throw new AssertionError("Wall placement changed");
  System.out.println("Ground placement regression checks passed");
 }
}
