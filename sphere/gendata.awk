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
	print ix"\t"iy"\t"iz;
#	printf ("(%14.10fd, %14.10fd)\n", lon, lat);
}
