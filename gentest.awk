BEGIN{
  for (i = 0; i < 100000; i++)
  {
    x = 1000 * int(1000 * rand());
    y = 1000 * int(1000 * rand());
    print "select count(1) from test_points where x >= "x" and x <= "x+1000" and y >= "y" and y <= "y+1000";";
#    print "select zcurve_oids_by_extent("x","y","x+100","y+100") as count;";
#    print "select zcurve_oids_by_extent_ii("x","y","x+1000","y+1000") as count;";
#    print "select count(1) from test_points where point(x,y) <@ box(point("x","y"),point("x+1000","y+1000"));";
#     print "EXPLAIN (ANALYZE,BUFFERS) select count(1) from test_pt where point <@ box(point("x","y"),point("x+10000","y+10000"));";
#    print "select "i";";
#    print "select count(1) from zcurve_2d_lookup('zcurve_test_points', "x","y","x+1000","y+1000");";
#    EXPLAIN (ANALYZE,BUFFERS) 
  }
}
