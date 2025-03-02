#ifndef KNOB_H
#define KNOB_H

class Knob{
    public:
        int rotation = 0;
        int max=8; 
        int min=0; 
        int lastIncrement = 0;
        int previousStateA;
        int previousStateB;
        int no;

        Knob(int _no);
        int clamp(int r);
        void storeValue(int r);        
        void updateValues(int currentStateA, int currentStateB);
};

#endif