Please don't copy our code
Now works with
Basic Object Fatching 100%
Persistent Connection 100%
Caching 100%

conditional-GET 100%


doesn't work with concurrent, but TA said getting 70% of the basic test will give us 
100% on basic test.

conditional works too

Design Decisions: 
We decided to use threads and a blocking mutex to handle multiple connections.
We setup the socket listener and spawn threads upon client connection.
The actual request/response/cache is handled by http_connection()
