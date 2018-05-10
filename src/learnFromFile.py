import sys
import nltk
import os

#Takes in learning file and moves sentences into prolog format.
def learnFromFile(learningFile, prologFile):
    

    print("Learning facts and rules from " + learningFile + "...\n")
    #Extracts current words in lexicon to maintain old knowledge and prevent overwriting of new knowledge.
    knowledge = {}

    extractOld = open(prologFile, "r")
    inLexicon = False
    
    for line in extractOld:
        line = line.strip()
        
        if(inLexicon):
            if(line != ''):
                curWord = line.split('-->')[1]
                curRepr = line.split('-->')[0]

                curWord = curWord.strip().strip('.').strip('[').strip(']')

                knowledge[curWord] = curRepr.strip()

        if('Lexicon' in line):
            inLexicon = True

                
    extractOld.close()



    #Extracts sentences from simple file and converts to prolog fact format, currently supports files made up of declaritive sentences.
    tmpLearning = open(learningFile, 'r')
    learning = ' '.join(tmpLearning.readlines())
    sentences = learning.split('.')
    tmpLearning.close()
    
    for curSent in sentences:
        text = nltk.word_tokenize(curSent)
        tagged = nltk.pos_tag(text)

        for curW in range(len(tagged)):
            word = tagged[curW][0].lower()
            pos = tagged[curW][1]

            if(word not in knowledge):
                if('VB' in pos):
                    if(curW == len(tagged)-1 or 'NN' not in tagged[curW +1][1]):
                        knowledge[word] = 'vi(l(X,' + word + '(X)))'
                    else:
                        knowledge[word] = 'vt(l(X,l(Y,' + word + '(Y,X))))'
                elif('NN' in pos):
                    knowledge[word] = 'n(l(X,' + word + '(X)))'




    curProlog = open(prologFile, 'r')
    newProlog = open(prologFile + 'tmp', 'w')

    #print all none lexicon information back to the prolog file
    for line in curProlog:
        newProlog.write(line)

        if('Lexicon' in line):
            break


    curProlog.close()
    
    for key in sorted(knowledge, key=lambda k: knowledge[k], reverse = True):
        outLine = '    ' + knowledge[key] +  ' --> [' + key + '].'
        newProlog.write(outLine + '\n')


    newProlog.close()

    os.rename(prologFile + 'tmp', prologFile)


    

if(len(sys.argv) < 3):
    print('Please include file to learn from as well as current prolog file. python learnFromFile.py newKnowledge.txt prologDatabase.pl')
else:
    learnFromFile(sys.argv[1], sys.argv[2])

