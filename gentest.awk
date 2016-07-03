BEGIN{
  for (i = 0; i < 10000; i++)
  {
    x = 1000 * int(1000 * rand());
    y = 1000 * int(1000 * rand());
#    print "select count(1) from test_points where x >= "x" and x <= "x+100" and y >= "y" and y <= "y+100";";
#    print "select zcurve_oids_by_extent("x","y","x+100","y+100") as count;";
    print "select zcurve_oids_by_extent_ii("x","y","x+1000","y+1000") as count;";
#    print "select count(1) from test_points where point(x,y) <@ box(point("x","y"),point("x+1000","y+1000"));";
  }
}
