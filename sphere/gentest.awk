BEGIN{
	pi=3.1415926535897932;
	degra=pi/180.0;
	rad=180.0/pi;
	Grad = 1000000.;
}
{
	x = $1;
	y = $2;
	z = $3;
	r3 = sqrt(x*x + y*y + z*z);
	x *= Grad / r3;
	y *= Grad / r3;
	z *= Grad / r3;

	r2 = sqrt(x*x + y*y);

	lat = atan2(z, r2) * rad;
	lon = 180. + atan2(y, x) * rad;

	ix = int(x+0.5+Grad);
	iy = int(y+0.5+Grad);
	iz = int(z+0.5+Grad);
#	print ix"\t"iy"\t"iz;
#	printf ("(%14.10fd, %14.10fd)\n", lon, lat);
#	EXPLAIN (ANALYZE,BUFFERS) 
#	printf ("select count(1) from spoint_data where sp @'<(%14.10fd,%14.10fd),.316d>'::scircle;\n", lon, lat);
	lrad = int(0.5 + Grad * sin(1. * degra));
#	print "select count(1) from zcurve_3d_lookup_tidonly('zcurve_num_test_points_3d', "ix-lrad","iy-lrad","iz-lrad","ix+lrad","iy+lrad","iz+lrad");";
	print "select count(1) from hilbert_3d_lookup_tidonly('hilbert_num_test_points_3d', "ix-lrad","iy-lrad","iz-lrad","ix+lrad","iy+lrad","iz+lrad");";}
