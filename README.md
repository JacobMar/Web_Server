/*CSci4061 S2016 Assignment 4
 *Name: Jacob Martin, Julia Nelson, Nadine Garcia
 *X500: mart3236, nels8117, garci597*/

To run our program you must be on a Linux Lab machine with gcc installed. Navigate into our project directory and run "make". Use ./web_server port path-to-testing/testing num_dispatch num_workers queue_len. The port number must be between 1025 and 65535 and the number of dispatch, number of workers, and queue length must be no greater than 100. In another terminal, run wget http://127.0.0.1:port/path-to-file-from-root.

Our program first initializes the connection acception/handling system. It then creates the dispatcher and worker threads. The dispatcher threads accept the incoming connection, read the request from the connection, and then place the request in a queue for the worker thread to pick up and serve. The worker threads monitor the request queue, pick up the requests from it, and serve those requests back to the client. A log is kept as the worker threads serve each request.
