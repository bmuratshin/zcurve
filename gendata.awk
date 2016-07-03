BEGIN{
  for (i = 0; i < 100000000; i++)
  {
    x = int(1000000 * rand());
    y = int(1000000 * rand());
#    print "insert into test_points (x,y) values("x","y");";
#    print x"\t"y;
    print "("x","y")";
  }
}
